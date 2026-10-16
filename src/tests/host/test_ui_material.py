#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_material.py
# Brief: Check material dependency indexing and structural bounds without executing native shaders.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/scripts"))
import ui_material


class MaterialInventory(unittest.TestCase):
    def test_explicit_constant_material_and_native_name_normalization(self):
        data = b'material GUI\\Panel.TGA { { blend blend map textures/panel.tga clamp vertexColor } }'
        result = ui_material.index_materials(data)
        self.assertEqual(list(result), ["gui/panel"])
        material = result["gui/panel"]
        self.assertEqual(material["definition"], "explicit")
        self.assertEqual(material["original_name"], "GUI\\Panel.TGA")
        self.assertEqual(material["stage_count"], 1)
        self.assertEqual(material["map_resources"], ["textures/panel.tga"])
        self.assertEqual(material["blends"], [["blend"]])
        self.assertFalse(material["dynamic_expression"])
        self.assertEqual(material["sha256"], hashlib.sha256(data).hexdigest())
        self.assertNotIn("vertexColor", json.dumps(material))
        self.assertIn("inventory-only", material["compatibility"])

    def test_quotes_comments_and_multichar_literals_do_not_change_brace_depth(self):
        data = (b'// fake {\n/* } fake { */\n"gui/panel" {\n'
                b'qer_description "literal { // map decoy }"\n'
                b'qer_editorimage textures/editor.tga\n'
                b'{ name \'stage { }\' /* } */ map "textures/actual.tga" }\n}\n')
        result = ui_material.index_materials(data)["gui/panel"]
        self.assertEqual(result["stage_count"], 1)
        self.assertEqual(result["map_resources"], ["textures/actual.tga"])
        self.assertEqual(result["editor_image_resources"], ["textures/editor.tga"])
        self.assertEqual(result["line"], 3)

    def test_known_non_material_declarations_are_balanced_and_excluded(self):
        result = ui_material.index_materials(b'table wave { { 0,1,0 } }\n'
                                             b'entityDef sample { "key" "value" }\n'
                                             b'gui/test { { map _white } }')
        self.assertEqual(list(result), ["gui/test"])

    def test_first_duplicate_definition_wins_with_conflicting_hash_evidence(self):
        data = b'GUI/test.tga { { map _white } }\ngui/test { { map _black } }'
        result = ui_material.index_materials(data)["gui/test"]
        self.assertEqual(result["map_resources"], ["_white"])
        self.assertEqual(len(result["duplicate_definitions"]), 1)
        self.assertNotEqual(result["sha256"], result["duplicate_definitions"][0]["sha256"])

    def test_dynamic_registers_blend_pair_and_renderer_feature_candidates(self):
        result = ui_material.index_materials(b'test { { blend GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA '
                                             b'colored rgb parm0 if time>0 map _white scroll time*0.2,0 } }')["test"]
        self.assertEqual(result["blends"], [["gl_src_alpha", "gl_one_minus_src_alpha"]])
        self.assertTrue(result["dynamic_expression"])
        self.assertEqual(result["features"], ["conditional-stage", "entity-color-registers", "texture-matrix"])
        self.assertEqual(result["unsupported_identifiers"], ["colored", "if", "scroll"])

    def test_literal_video_loop_sound_and_shader_fragment_dependencies(self):
        data = (b'test { { videoMap loop video/menu.bik } { soundMap sound/ui } '
                b'{ program gui fragmentMap 0 nearest clamp textures/gui.tga } }')
        result = ui_material.index_materials(data)["test"]
        self.assertEqual(result["stage_count"], 3)
        self.assertEqual(result["video_resources"], ["video/menu.bik"])
        self.assertEqual(result["sound_resources"], ["sound/ui"])
        self.assertEqual(result["map_resources"], ["textures/gui.tga"])
        self.assertEqual(result["features"], ["cinematic", "shader-program"])

    def test_nested_image_programs_record_literal_candidates_and_unknown_operators(self):
        result = ui_material.index_materials(b'test { { map addnormals(textures/a, '
                                             b'heightmap(textures/b, 2)) } '
                                             b'{ map futureop(textures/c, 0.5) } }')["test"]
        self.assertEqual(result["map_resources"], ["textures/a", "textures/b", "textures/c"])
        self.assertEqual(result["image_operators"], ["addnormals", "futureop", "heightmap"])
        self.assertEqual(result["unrecognized_image_operators"], ["futureop"])
        self.assertIn("image-program", result["features"])

    def test_shortcut_lighting_maps_count_declared_stages_without_claiming_native_fixups(self):
        result = ui_material.index_materials(b'test { diffuseMap textures/d '
                                             b'bumpMap textures/n { blend add map textures/a } }')["test"]
        self.assertEqual(result["stage_count"], 3)
        self.assertEqual(result["stage_count_kind"], "syntactic-before-native-implicit-stages")
        self.assertEqual(result["map_resources"], ["textures/a", "textures/d", "textures/n"])
        self.assertIn("lighting-stage", result["features"])

    def test_unsafe_resource_paths_are_reported_without_dropping_other_materials(self):
        result = ui_material.index_materials(b'bad { { map /textures/absolute.tga } }\n'
                                             b'good { { map textures/relative.tga } }')
        self.assertEqual(result["bad"]["map_resources"], [])
        self.assertEqual(result["bad"]["unresolved_resources"],
                         [{"literal": "/textures/absolute.tga", "line": 1,
                           "reason": "resource-name-outside-relative-profile"}])
        self.assertEqual(result["good"]["map_resources"], ["textures/relative.tga"])

    def test_implicit_material_is_distinct_and_preserves_native_dynamic_colored_default(self):
        index = ui_material.index_materials(b'guiSolid { { blend blend colored map _white } }')
        explicit = ui_material.resolve_material(index, "GUISOLID")
        implicit = ui_material.resolve_material(index, "Generated/SWF/Shell.TGA")
        self.assertEqual(explicit["definition"], "explicit")
        self.assertEqual(explicit["map_resources"], ["_white"])
        self.assertEqual(implicit["definition"], "implicit-image-default")
        self.assertEqual(implicit["name"], "generated/swf/shell")
        self.assertEqual(implicit["map_resources"], ["generated/swf/shell"])
        self.assertTrue(implicit["dynamic_expression"])
        self.assertNotIn("sha256", implicit)

    def test_native_canonicalization_removes_the_last_dot_suffix(self):
        self.assertEqual(ui_material.canonical_material_name("GUI\\TEST.JPEG"), "gui/test")
        self.assertEqual(ui_material.canonical_material_name("gui.v1/test"), "gui")
        for value in ("../gui/test", "/gui/test", "gui/../test", ".tga", "gui\x00test", "gui/", "g\u2603i/test"):
            with self.subTest(name=value), self.assertRaises(ValueError):
                ui_material.canonical_material_name(value)

    def test_native_material_identity_keeps_repeated_separators_distinct_from_resource_aliases(self):
        self.assertEqual(ui_material.canonical_material_name("GUI\\\\TEST.TGA"), "gui//test")
        index = ui_material.index_materials(b'gui//test { { map textures//first.tga } }\n'
                                             b'gui/test { { map textures/second.tga } }\n// comment')
        self.assertEqual(sorted(index), ["gui//test", "gui/test"])
        self.assertEqual(index["gui//test"]["map_resources"], ["textures/first.tga"])
        self.assertEqual(index["gui/test"]["map_resources"], ["textures/second.tga"])
        self.assertEqual(index["gui//test"]["duplicate_definitions"], [])
        self.assertEqual(ui_material.resolve_material(index, "GUI\\\\TEST.TGA")["name"], "gui//test")
        implicit = ui_material.resolve_material({}, "gui//absent.tga")
        self.assertEqual(implicit["name"], "gui//absent")
        self.assertEqual(implicit["map_resources"], ["gui/absent"])

    def test_unbalanced_structure_comment_quote_and_missing_arguments_are_rejected(self):
        for data in (b'name', b'name {', b'name }', b'{ }', b'name "{" }',
                     b'name { } }', b'/* comment', b'name { "bad }', b'name { "line\nnext" }',
                     b'name { { map } }', b'name { { blend } }', b'name { { blend one, } }',
                     b'name { { videoMap loop } }', b'name { { fragmentMap x _white } }',
                     b'name { { map add(textures/a, 1 } }', b'name { { map _white } }\x00'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                ui_material.index_materials(data)

    def test_inspection_limits_bound_bytes_tokens_declarations_and_nesting(self):
        cases = (("MAX_MATERIAL_BYTES", 3, b'name {}'),
                 ("MAX_TOKEN_BYTES", 3, b'longname {}'),
                 ("MAX_MATERIAL_TOKENS", 2, b'name {}'),
                 ("MAX_MATERIAL_DECLARATIONS", 1, b'a {} b {}'),
                 ("MAX_BRACE_DEPTH", 1, b'a { {} }'))
        for constant, limit, data in cases:
            with self.subTest(bound=constant), mock.patch.object(ui_material, constant, limit), self.assertRaises(ValueError):
                ui_material.index_materials(data)


if __name__ == "__main__":
    unittest.main()
