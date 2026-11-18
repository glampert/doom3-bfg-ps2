/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "../../idlib/precompiled.h"


// [PS2_D3BFG]: The host script cook compiles the compiler against a minimal game surface.
#if defined( ID_SCRIPT_COOK )
#include "tools/script_cook/cook_game.h"
#else
#include "../Game_local.h"
#endif

// [PS2_D3BFG]: Required compilation failures release owned state before fatal termination.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
#include "ps2/game/script_error.h"
#endif

// [PS2_D3BFG]: The PS2 loads the cooked program instead of compiling it.
#if defined( ID_SCRIPT_IMAGE ) || defined( ID_SCRIPT_CHECK )
#include "ps2/game/script_image.h"
#endif

// [PS2_D3BFG]: Pointer-sized script types take the EE's 4 bytes, also in the 64-bit host script
// cook, whose program image the PS2 loads.
static const int SCRIPT_POINTER_SIZE = 4;
#if defined( ID_PS2 )
static_assert( sizeof( void * ) == SCRIPT_POINTER_SIZE && sizeof( int * ) == SCRIPT_POINTER_SIZE, "script pointer size" );
#endif

// simple types.  function types are dynamically allocated
idTypeDef	type_void( ev_void, &def_void, "void", 0, NULL );
idTypeDef	type_scriptevent( ev_scriptevent, &def_scriptevent, "scriptevent", SCRIPT_POINTER_SIZE, NULL );
idTypeDef	type_namespace( ev_namespace, &def_namespace, "namespace", SCRIPT_POINTER_SIZE, NULL );
idTypeDef	type_string( ev_string, &def_string, "string", MAX_STRING_LEN, NULL );
idTypeDef	type_float( ev_float, &def_float, "float", sizeof( float ), NULL );
idTypeDef	type_vector( ev_vector, &def_vector, "vector", sizeof( idVec3 ), NULL );
idTypeDef	type_entity( ev_entity, &def_entity, "entity", SCRIPT_POINTER_SIZE, NULL );					// stored as entity number pointer
idTypeDef	type_field( ev_field, &def_field, "field", SCRIPT_POINTER_SIZE, NULL );
idTypeDef	type_function( ev_function, &def_function, "function", SCRIPT_POINTER_SIZE, &type_void );
idTypeDef	type_virtualfunction( ev_virtualfunction, &def_virtualfunction, "virtual function", sizeof( int ), NULL );
idTypeDef	type_pointer( ev_pointer, &def_pointer, "pointer", SCRIPT_POINTER_SIZE, NULL );
idTypeDef	type_object( ev_object, &def_object, "object", SCRIPT_POINTER_SIZE, NULL );					// stored as entity number pointer
idTypeDef	type_jumpoffset( ev_jumpoffset, &def_jumpoffset, "<jump>", sizeof( int ), NULL );		// only used for jump opcodes
idTypeDef	type_argsize( ev_argsize, &def_argsize, "<argsize>", sizeof( int ), NULL );				// only used for function call and thread opcodes
idTypeDef	type_boolean( ev_boolean, &def_boolean, "boolean", sizeof( int ), NULL );

idVarDef	def_void( &type_void );
idVarDef	def_scriptevent( &type_scriptevent );
idVarDef	def_namespace( &type_namespace );
idVarDef	def_string( &type_string );
idVarDef	def_float( &type_float );
idVarDef	def_vector( &type_vector );
idVarDef	def_entity( &type_entity );
idVarDef	def_field( &type_field );
idVarDef	def_function( &type_function );
idVarDef	def_virtualfunction( &type_virtualfunction );
idVarDef	def_pointer( &type_pointer );
idVarDef	def_object( &type_object );
idVarDef	def_jumpoffset( &type_jumpoffset );		// only used for jump opcodes
idVarDef	def_argsize( &type_argsize );
idVarDef	def_boolean( &type_boolean );

// [PS2_D3BFG]: The compiler's program structures; the PS2 runs the cooked image's (below).
#if !defined( ID_SCRIPT_IMAGE )

/***********************************************************************

  function_t

***********************************************************************/

/*
================
function_t::function_t
================
*/
function_t::function_t() {
	Clear();
}

/*
================
function_t::Allocated
================
*/
size_t function_t::Allocated() const {
	return name.Allocated() + parmSize.Allocated();
}

/*
================
function_t::SetName
================
*/
void function_t::SetName( const char *name ) {
	this->name = name;
}

/*
================
function_t::Name
================
*/
const char *function_t::Name() const {
	return name;
}

/*
================
function_t::Clear
================
*/
void function_t::Clear() {
	eventdef		= NULL;
	def				= NULL;
	type			= NULL;
	firstStatement	= 0;
	numStatements	= 0;
	parmTotal		= 0;
	locals			= 0;
	filenum			= 0;
	name.Clear();
	parmSize.Clear();
}

/***********************************************************************

  idTypeDef

***********************************************************************/

/*
================
idTypeDef::idTypeDef
================
*/
idTypeDef::idTypeDef( etype_t etype, idVarDef *edef, const char *ename, int esize, idTypeDef *aux ) {
	name		= ename;
	type		= etype;
	def			= edef;
	size		= esize;
	auxType		= aux;
	
	parmTypes.SetGranularity( 1 );
	parmNames.SetGranularity( 1 );
	functions.SetGranularity( 1 );
}

/*
================
idTypeDef::idTypeDef
================
*/
idTypeDef::idTypeDef( const idTypeDef &other ) {
	*this = other;
}

/*
================
idTypeDef::operator=
================
*/
void idTypeDef::operator=( const idTypeDef& other ) {
	type		= other.type;
	def			= other.def;
	name		= other.name;
	size		= other.size;
	auxType		= other.auxType;
	parmTypes	= other.parmTypes;
	parmNames	= other.parmNames;
	functions	= other.functions;
}

/*
================
idTypeDef::Allocated
================
*/
size_t idTypeDef::Allocated() const {
	size_t memsize;
	int i;

	memsize = name.Allocated() + parmTypes.Allocated() + parmNames.Allocated() + functions.Allocated();
	for( i = 0; i < parmTypes.Num(); i++ ) {
		memsize += parmNames[ i ].Allocated();
	}

	return memsize;
}

/*
================
idTypeDef::Inherits

Returns true if basetype is an ancestor of this type.
================
*/
bool idTypeDef::Inherits( const idTypeDef *basetype ) const {
	idTypeDef *superType;

	if ( type != ev_object ) {
		return false;
	}

	if ( this == basetype ) {
		return true;
	}
	for( superType = auxType; superType != NULL; superType = superType->auxType ) {
		if ( superType == basetype ) {
			return true;
		}
	}

	return false;
}

/*
================
idTypeDef::MatchesType

Returns true if both types' base types and parameters match
================
*/
bool idTypeDef::MatchesType( const idTypeDef &matchtype ) const {
	int i;

	if ( this == &matchtype ) {
		return true;
	}

	if ( ( type != matchtype.type ) || ( auxType != matchtype.auxType ) ) {
		return false;
	}

	if ( parmTypes.Num() != matchtype.parmTypes.Num() ) {
		return false;
	}

	for( i = 0; i < matchtype.parmTypes.Num(); i++ ) {
		if ( parmTypes[ i ] != matchtype.parmTypes[ i ] ) {
			return false;
		}
	}

	return true;
}

/*
================
idTypeDef::MatchesVirtualFunction

Returns true if both functions' base types and parameters match
================
*/
bool idTypeDef::MatchesVirtualFunction( const idTypeDef &matchfunc ) const {
	int i;

	if ( this == &matchfunc ) {
		return true;
	}

	if ( ( type != matchfunc.type ) || ( auxType != matchfunc.auxType ) ) {
		return false;
	}

	if ( parmTypes.Num() != matchfunc.parmTypes.Num() ) {
		return false;
	}

	if ( parmTypes.Num() > 0 ) {
		if ( !parmTypes[ 0 ]->Inherits( matchfunc.parmTypes[ 0 ] ) ) {
			return false;
		}
	}

	for( i = 1; i < matchfunc.parmTypes.Num(); i++ ) {
		if ( parmTypes[ i ] != matchfunc.parmTypes[ i ] ) {
			return false;
		}
	}

	return true;
}

/*
================
idTypeDef::AddFunctionParm

Adds a new parameter for a function type.
================
*/
void idTypeDef::AddFunctionParm( idTypeDef *parmtype, const char *name ) {
	if ( type != ev_function ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::AddFunctionParm : tried to add parameter on non-function type" );
#else
		throw idCompileError( "idTypeDef::AddFunctionParm : tried to add parameter on non-function type" );
#endif
	}

	parmTypes.Append( parmtype );
	idStr &parmName = parmNames.Alloc();
	parmName = name;
}

/*
================
idTypeDef::AddField

Adds a new field to an object type.
================
*/
void idTypeDef::AddField( idTypeDef *fieldtype, const char *name ) {
	if ( type != ev_object ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::AddField : tried to add field to non-object type" );
#else
		throw idCompileError( "idTypeDef::AddField : tried to add field to non-object type" );
#endif
	}

	parmTypes.Append( fieldtype );
	idStr &parmName = parmNames.Alloc();
	parmName = name;

	if ( fieldtype->FieldType()->Inherits( &type_object ) ) {
		size += type_object.Size();
	} else {
		size += fieldtype->FieldType()->Size();
	}
}

/*
================
idTypeDef::SetName
================
*/
void idTypeDef::SetName( const char *newname ) {
	name = newname;
}

/*
================
idTypeDef::Name
================
*/
const char *idTypeDef::Name() const {
	return name;
}

/*
================
idTypeDef::Type
================
*/
etype_t idTypeDef::Type() const {
	return type;
}

/*
================
idTypeDef::Size
================
*/
int idTypeDef::Size() const {
	return size;
}

/*
================
idTypeDef::SuperClass

If type is an object, then returns the object's superclass
================
*/
idTypeDef *idTypeDef::SuperClass() const {
	if ( type != ev_object ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::SuperClass : tried to get superclass of a non-object type" );
#else
		throw idCompileError( "idTypeDef::SuperClass : tried to get superclass of a non-object type" );
#endif
	}

	return auxType;
}

/*
================
idTypeDef::ReturnType

If type is a function, then returns the function's return type
================
*/
idTypeDef *idTypeDef::ReturnType() const {
	if ( type != ev_function ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::ReturnType: tried to get return type on non-function type" );
#else
		throw idCompileError( "idTypeDef::ReturnType: tried to get return type on non-function type" );
#endif
	}

	return auxType;
}

/*
================
idTypeDef::SetReturnType

If type is a function, then sets the function's return type
================
*/
void idTypeDef::SetReturnType( idTypeDef *returntype ) {
	if ( type != ev_function ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::SetReturnType: tried to set return type on non-function type" );
#else
		throw idCompileError( "idTypeDef::SetReturnType: tried to set return type on non-function type" );
#endif
	}

	auxType = returntype;
}

/*
================
idTypeDef::FieldType

If type is a field, then returns it's type
================
*/
idTypeDef *idTypeDef::FieldType() const {
	if ( type != ev_field ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::FieldType: tried to get field type on non-field type" );
#else
		throw idCompileError( "idTypeDef::FieldType: tried to get field type on non-field type" );
#endif
	}

	return auxType;
}

/*
================
idTypeDef::SetFieldType

If type is a field, then sets the function's return type
================
*/
void idTypeDef::SetFieldType( idTypeDef *fieldtype ) {
	if ( type != ev_field ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::SetFieldType: tried to set return type on non-function type" );
#else
		throw idCompileError( "idTypeDef::SetFieldType: tried to set return type on non-function type" );
#endif
	}

	auxType = fieldtype;
}

/*
================
idTypeDef::PointerType

If type is a pointer, then returns the type it points to
================
*/
idTypeDef *idTypeDef::PointerType() const {
	if ( type != ev_pointer ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::PointerType: tried to get pointer type on non-pointer" );
#else
		throw idCompileError( "idTypeDef::PointerType: tried to get pointer type on non-pointer" );
#endif
	}

	return auxType;
}

/*
================
idTypeDef::SetPointerType

If type is a pointer, then sets the pointer's type
================
*/
void idTypeDef::SetPointerType( idTypeDef *pointertype ) {
	if ( type != ev_pointer ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", "idTypeDef::SetPointerType: tried to set type on non-pointer" );
#else
		throw idCompileError( "idTypeDef::SetPointerType: tried to set type on non-pointer" );
#endif
	}

	auxType = pointertype;
}

/*
================
idTypeDef::NumParameters
================
*/
int idTypeDef::NumParameters() const {
	return parmTypes.Num();
}

/*
================
idTypeDef::GetParmType
================
*/
idTypeDef *idTypeDef::GetParmType( int parmNumber ) const {
	assert( parmNumber >= 0 );
	assert( parmNumber < parmTypes.Num() );
	return parmTypes[ parmNumber ];
}

/*
================
idTypeDef::GetParmName
================
*/
const char *idTypeDef::GetParmName( int parmNumber ) const {
	assert( parmNumber >= 0 );
	assert( parmNumber < parmTypes.Num() );
	return parmNames[ parmNumber ];
}

/*
================
idTypeDef::NumFunctions
================
*/
int idTypeDef::NumFunctions() const {
	return functions.Num();
}

/*
================
idTypeDef::GetFunctionNumber
================
*/
int idTypeDef::GetFunctionNumber( const function_t *func ) const {
	int i;

	for( i = 0; i < functions.Num(); i++ ) {
		if ( functions[ i ] == func ) {
			return i;
		}
	}
	return -1;
}

/*
================
idTypeDef::GetFunction
================
*/
const function_t *idTypeDef::GetFunction( int funcNumber ) const {
	assert( funcNumber >= 0 );
	assert( funcNumber < functions.Num() );
	return functions[ funcNumber ];
}

/*
================
idTypeDef::AddFunction
================
*/
void idTypeDef::AddFunction( const function_t *func ) {
	int i;

	for( i = 0; i < functions.Num(); i++ ) {
		if ( !strcmp( functions[ i ]->def->Name(), func->def->Name() ) ) {
			if ( func->def->TypeDef()->MatchesVirtualFunction( *functions[ i ]->def->TypeDef() ) ) {
				functions[ i ] = func;
				return;
			}
		}
	}
	functions.Append( func );
}

/***********************************************************************

  idVarDef

***********************************************************************/

/*
================
idVarDef::idVarDef()
================
*/
idVarDef::idVarDef( idTypeDef *typeptr ) {
	typeDef		= typeptr;
	num			= 0;
	scope		= NULL;
	numUsers	= 0;
	initialized = idVarDef::uninitialized;
	memset( &value, 0, sizeof( value ) );
	name		= NULL;
	next		= NULL;
}

/*
============
idVarDef::~idVarDef
============
*/
idVarDef::~idVarDef() {
	if ( name ) {
		name->RemoveDef( this );
	}
}

/*
============
idVarDef::Name
============
*/
const char *idVarDef::Name() const {
	return name->Name();
}

/*
============
idVarDef::GlobalName
============
*/
const char *idVarDef::GlobalName() const {
	if ( scope != &def_namespace ) {
		return va( "%s::%s", scope->GlobalName(), name->Name() );
	} else {
		return name->Name();
	}
}

/*
============
idVarDef::DepthOfScope
============
*/
int idVarDef::DepthOfScope( const idVarDef *otherScope ) const {
	const idVarDef *def;
	int depth;

	depth = 1;
	for( def = otherScope; def != NULL; def = def->scope ) {
		if ( def == scope ) {
			return depth;
		}
		depth++;
	}

	return 0;
}

/*
============
idVarDef::SetFunction
============
*/
void idVarDef::SetFunction( function_t *func ) {
	assert( typeDef );
	initialized = initializedConstant;
	assert( typeDef->Type() == ev_function );
	value.functionPtr = func;
}

/*
============
idVarDef::SetObject
============
*/
void idVarDef::SetObject( idScriptObject *object ) {
	assert( typeDef );
	initialized = initialized;
	assert( typeDef->Inherits( &type_object ) );
#if defined( ID_SCRIPT_COOK )
	// [PS2_D3BFG]: The compiler only clears object slots, which hold 4 bytes in the host cook too.
	assert( object == NULL );
	*value.entityNumberPtr = 0;
#else
	*value.objectPtrPtr = object;
#endif
}

/*
============
idVarDef::SetValue
============
*/
void idVarDef::SetValue( const eval_t &_value, bool constant ) {
	assert( typeDef );
	if ( constant ) {
		initialized = initializedConstant;
	} else {
		initialized = initializedVariable;
	}

	switch( typeDef->Type() ) {
	case ev_pointer :
	case ev_boolean :
	case ev_field :
		*value.intPtr = _value._int;
		break;

	case ev_jumpoffset :
		value.jumpOffset = _value._int;
		break;

	case ev_argsize :
		value.argSize = _value._int;
		break;

	case ev_entity :
		*value.entityNumberPtr = _value.entity;
		break;

	case ev_string :
		idStr::Copynz( value.stringPtr, _value.stringPtr, MAX_STRING_LEN );
		break;

	case ev_float :
		*value.floatPtr = _value._float;
		break;

	case ev_vector :
		value.vectorPtr->x = _value.vector[ 0 ];
		value.vectorPtr->y = _value.vector[ 1 ];
		value.vectorPtr->z = _value.vector[ 2 ];
		break;

	case ev_function :
		value.functionPtr = _value.function;
		break;

	case ev_virtualfunction :
		value.virtualFunction = _value._int;
		break;

	case ev_object :
		*value.entityNumberPtr = _value.entity;
		break;

	default :
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", va( "weird type on '%s'", Name() ) );
#else
		throw idCompileError( va( "weird type on '%s'", Name() ) );
#endif
		break;
	}
}

/*
============
idVarDef::SetString
============
*/
void idVarDef::SetString( const char *string, bool constant ) {
	if ( constant ) {
		initialized = initializedConstant;
	} else {
		initialized = initializedVariable;
	}
	
	assert( typeDef && ( typeDef->Type() == ev_string ) );
	idStr::Copynz( value.stringPtr, string, MAX_STRING_LEN );
}

/*
============
idVarDef::PrintInfo
============
*/
void idVarDef::PrintInfo( idFile *file, int instructionPointer ) const {
	statement_t	*jumpst;
	int			jumpto;
	etype_t		etype;
	int			i;
	int			len;
	const char	*ch;

	if ( initialized == initializedConstant ) {
		file->Printf( "const " );
	}

	etype = typeDef->Type();
	switch( etype ) {
	case ev_jumpoffset :
		jumpto = instructionPointer + value.jumpOffset;
		jumpst = &gameLocal.program.GetStatement( jumpto );
		file->Printf( "address %d [%s(%d)]", jumpto, gameLocal.program.GetFilename( jumpst->file ), jumpst->linenumber );
		break;

	case ev_function :
		if ( value.functionPtr->eventdef ) {
			file->Printf( "event %s", GlobalName() );
		} else {
			file->Printf( "function %s", GlobalName() );
		}
		break;

	case ev_field :
		file->Printf( "field %d", value.ptrOffset );
		break;

	case ev_argsize:
		file->Printf( "args %d", value.argSize );
		break;

	default:
		file->Printf( "%s ", typeDef->Name() );
		if ( initialized == initializedConstant ) {
			switch( etype ) {
			case ev_string :
				file->Printf( "\"" );
				len = strlen( value.stringPtr );
				ch = value.stringPtr;
				for( i = 0; i < len; i++, ch++ ) {
					if ( idStr::CharIsPrintable( *ch ) ) {
						file->Printf( "%c", *ch );
					} else if ( *ch == '\n' ) {
						file->Printf( "\\n" );
					} else {
						file->Printf( "\\x%.2x", static_cast<int>( *ch ) );
					}
				}
				file->Printf( "\"" );
				break;

			case ev_vector :
				file->Printf( "'%s'", value.vectorPtr->ToString() );
				break;

			case ev_float :
                file->Printf( "%f", *value.floatPtr );
				break;

			case ev_virtualfunction :
				file->Printf( "vtable[ %d ]", value.virtualFunction );
				break;

			default :
				file->Printf( "%d", *value.intPtr );
				break;
			}
		} else if ( initialized == stackVariable ) {
			file->Printf( "stack[%d]", value.stackOffset );
		} else {
			file->Printf( "global[%d]", num );
		}
		break;
	}
}

/***********************************************************************

  idVarDef

***********************************************************************/

/*
============
idVarDefName::AddDef
============
*/
void idVarDefName::AddDef( idVarDef *def ) {
	assert( def->next == NULL );
	def->name = this;
	def->next = defs;
	defs = def;
}

/*
============
idVarDefName::RemoveDef
============
*/
void idVarDefName::RemoveDef( idVarDef *def ) {
	if ( defs == def ) {
		defs = def->next;
	} else {
		for ( idVarDef *d = defs; d->next != NULL; d = d->next ) {
			if ( d->next == def ) {
				d->next = def->next;
				break;
			}
		}
	}
	def->next = NULL;
	def->name = NULL;
}

#endif // !defined( ID_SCRIPT_IMAGE )

/***********************************************************************

  idScriptObject

***********************************************************************/

/*
============
idScriptObject::idScriptObject
============
*/
idScriptObject::idScriptObject() {
	data = NULL;
	type = &type_object;
}

/*
============
idScriptObject::~idScriptObject
============
*/
idScriptObject::~idScriptObject() {
	Free();
}

/*
============
idScriptObject::Free
============
*/
void idScriptObject::Free() {
	if ( data ) {
		Mem_Free( data );
	}

	data = NULL;
	type = &type_object;
}

/*
================
idScriptObject::Save
================
*/
void idScriptObject::Save( idSaveGame *savefile ) const {
	size_t size;

	if ( type == &type_object && data == NULL ) {
		// Write empty string for uninitialized object
		savefile->WriteString( "" );
	} else {
		savefile->WriteString( type->Name() );
		size = type->Size();
		savefile->WriteInt( size );
		savefile->Write( data, size );
	}
}

/*
================
idScriptObject::Restore
================
*/
void idScriptObject::Restore( idRestoreGame *savefile ) {
	idStr typeName;
	int size;

	savefile->ReadString( typeName );

	// Empty string signals uninitialized object
	if ( typeName.Length() == 0 ) {
		return;
	}

	if ( !SetType( typeName ) ) {
		savefile->Error( "idScriptObject::Restore: failed to restore object of type '%s'.", typeName.c_str() );
	}

	savefile->ReadInt( size );
	if ( size != type->Size() ) {
		savefile->Error( "idScriptObject::Restore: size of object '%s' doesn't match size in save game.", typeName.c_str() );
	}

	savefile->Read( data, size );
}

/*
============
idScriptObject::SetType

Allocates an object and initializes memory.
============
*/
bool idScriptObject::SetType( const char *typeName ) {
	size_t size;
	idTypeDef *newtype;

	// lookup the type
	newtype = gameLocal.program.FindType( typeName );

	// only allocate memory if the object type changes
	if ( newtype != type ) {	
		Free();
		if ( !newtype ) {
			gameLocal.Warning( "idScriptObject::SetType: Unknown type '%s'", typeName );
			return false;
		}

		if ( !newtype->Inherits( &type_object ) ) {
			gameLocal.Warning( "idScriptObject::SetType: Can't create object of type '%s'.  Must be an object type.", newtype->Name() );
			return false;
		}

		// set the type
		type = newtype;

		// allocate the memory
		size = type->Size();
		data = ( byte * )Mem_Alloc( size, TAG_SCRIPT );
	}

	// init object memory
	ClearObject();

	return true;
}

/*
============
idScriptObject::ClearObject

Resets the memory for the script object without changing its type.
============
*/
void idScriptObject::ClearObject() {
	size_t size;

	if ( type != &type_object ) {
		// init object memory
		size = type->Size();
		memset( data, 0, size );
	}
}

/*
============
idScriptObject::HasObject
============
*/
bool idScriptObject::HasObject() const {
	return ( type != &type_object );
}

/*
============
idScriptObject::GetTypeDef
============
*/
idTypeDef *idScriptObject::GetTypeDef() const {
	return type;
}

/*
============
idScriptObject::GetTypeName
============
*/
const char *idScriptObject::GetTypeName() const {
	return type->Name();
}

/*
============
idScriptObject::GetConstructor
============
*/
const function_t *idScriptObject::GetConstructor() const {
	const function_t *func;

	func = GetFunction( "init" );
	return func;
}

/*
============
idScriptObject::GetDestructor
============
*/
const function_t *idScriptObject::GetDestructor() const {
	const function_t *func;

	func = GetFunction( "destroy" );
	return func;
}

/*
============
idScriptObject::GetFunction
============
*/
const function_t *idScriptObject::GetFunction( const char *name ) const {
	const function_t *func;

	if ( type == &type_object ) {
		return NULL;
	}

	func = gameLocal.program.FindFunction( name, type );
	return func;
}

/*
============
idScriptObject::GetVariable
============
*/
byte *idScriptObject::GetVariable( const char *name, etype_t etype ) const {
	int				i;
	int				pos;
	const idTypeDef	*t = type;
	const idTypeDef	*parm;

	if ( t == &type_object || t == NULL ) {
		return NULL;
	}

	do {
		if ( t->SuperClass() != &type_object ) {
			pos = t->SuperClass()->Size();
		} else {
			pos = 0;
		}
		for( i = 0; i < t->NumParameters(); i++ ) {
			parm = t->GetParmType( i );
			if ( !strcmp( t->GetParmName( i ), name ) ) {
				if ( etype != parm->FieldType()->Type() ) {
					return NULL;
				}
				return &data[ pos ];
			}

			if ( parm->FieldType()->Inherits( &type_object ) ) {
				pos += type_object.Size();
			} else {
				pos += parm->FieldType()->Size();
			}
		}
		t = t->SuperClass();
	} while( t != NULL && ( t != &type_object ) );

	return NULL;
}

// [PS2_D3BFG]: The compiler's program structures; the PS2 runs the cooked image's (below).
#if !defined( ID_SCRIPT_IMAGE )

/***********************************************************************

  idProgram

***********************************************************************/

/*
============
idProgram::AllocType
============
*/
idTypeDef *idProgram::AllocType( idTypeDef &type ) {
	idTypeDef * newtype	= new (TAG_SCRIPT) idTypeDef( type ); 
	typesHash.Add( idStr::Hash( type.Name() ), types.Append( newtype ) );
	return newtype;
}

/*
============
idProgram::AllocType
============
*/
idTypeDef *idProgram::AllocType( etype_t etype, idVarDef *edef, const char *ename, int esize, idTypeDef *aux ) {
	idTypeDef * newtype	= new (TAG_SCRIPT) idTypeDef( etype, edef, ename, esize, aux );
	typesHash.Add( idStr::Hash( ename ), types.Append( newtype ) );
	return newtype;
}

/*
============
idProgram::GetType

Returns a preexisting complex type that matches the parm, or allocates
a new one and copies it out.
============
*/
idTypeDef *idProgram::GetType( idTypeDef &type, bool allocate ) {

	for ( int i = typesHash.First( idStr::Hash( type.Name() ) ); i != -1; i = typesHash.Next( i ) ) {
		if ( types[ i ]->MatchesType( type ) && !strcmp( types[ i ]->Name(), type.Name() ) ) {
			return types[ i ];
		}
	}

	if ( !allocate ) {
		return NULL;
	}

	// allocate a new one
	return AllocType( type );
}

/*
============
idProgram::FindType

Returns a preexisting complex type that matches the name, or returns NULL if not found
============
*/
idTypeDef *idProgram::FindType( const char *name ) {

	for ( int i = typesHash.First( idStr::Hash( name ) ); i != -1; i = typesHash.Next( i ) ) {
		idTypeDef * check = types[ i ];
		if ( !strcmp( check->Name(), name ) ) {
			return check;
		}
	}

	return NULL;
}

/*
============
idProgram::GetDefList
============
*/
idVarDef *idProgram::GetDefList( const char *name ) const {
	int i, hash;

	hash = varDefNameHash.GenerateKey( name, true );
	for ( i = varDefNameHash.First( hash ); i != -1; i = varDefNameHash.Next( i ) ) {
		if ( idStr::Cmp( varDefNames[i]->Name(), name ) == 0 ) {
			return varDefNames[i]->GetDefs();
		}
	}
	return NULL;
}

/*
============
idProgram::AddDefToNameList
============
*/
void idProgram::AddDefToNameList( idVarDef *def, const char *name ) {
	int i, hash;

	hash = varDefNameHash.GenerateKey( name, true );
	for ( i = varDefNameHash.First( hash ); i != -1; i = varDefNameHash.Next( i ) ) {
		if ( idStr::Cmp( varDefNames[i]->Name(), name ) == 0 ) {
			break;
		}
	}
	if ( i == -1 ) {
		i = varDefNames.Append( new (TAG_SCRIPT) idVarDefName( name ) );
		varDefNameHash.Add( hash, i );
	}
	varDefNames[i]->AddDef( def );
}

#if ( defined( ID_PS2 ) && PS2_D3BFG_DEBUG ) || defined( ID_SCRIPT_COOK )
// [PS2_D3BFG]: Debug-only census of what the global bytes hold, recorded as AllocDef places them,
// to size the cooked program image. Rows are what reserved the bytes, columns the etype + 1.
enum scriptGlobalUse_t {
	SCRIPT_GLOBAL_IMMEDIATE,
	SCRIPT_GLOBAL_FUNCTION,
	SCRIPT_GLOBAL_OBJECT_TYPE,
	SCRIPT_GLOBAL_NAMED,
	SCRIPT_GLOBAL_USES
};
static const char * const s_scriptGlobalUseNames[ SCRIPT_GLOBAL_USES ] = { "immediate", "function", "object type", "named" };
static const char * const s_scriptTypeNames[ ev_boolean + 2 ] = { "error", "void", "scriptevent", "namespace", "string", "float", "vector", "entity", "field", "function", "virtualfunction", "pointer", "object", "jumpoffset", "argsize", "boolean" };
static int s_scriptGlobalBytes[ SCRIPT_GLOBAL_USES ][ ev_boolean + 2 ];
static int s_scriptGlobalDefs[ SCRIPT_GLOBAL_USES ][ ev_boolean + 2 ];

static void CountGlobalBytes( const idTypeDef *type, const char *name, int bytes ) {
	const int column = type->Type() + 1;
	int use = SCRIPT_GLOBAL_NAMED;
	if ( idStr::Cmpn( name, "<IMMEDIATE>", 11 ) == 0 ) {
		use = SCRIPT_GLOBAL_IMMEDIATE;
	} else if ( type->Type() == ev_function ) {
		use = SCRIPT_GLOBAL_FUNCTION;
	} else if ( type->Type() == ev_object && type->def == NULL ) {
		// ParseObjectDef allocates the type's own def before it assigns type->def.
		use = SCRIPT_GLOBAL_OBJECT_TYPE;
	}
	s_scriptGlobalBytes[ use ][ column ] += bytes;
	s_scriptGlobalDefs[ use ][ column ]++;
}
#endif

/*
============
idProgram::AllocDef
============
*/
idVarDef *idProgram::AllocDef( idTypeDef *type, const char *name, idVarDef *scope, bool constant ) {
	idVarDef	*def;
	idStr		element;
	idVarDef	*def_x;
	idVarDef	*def_y;
	idVarDef	*def_z;

	// allocate a new def
	def = new (TAG_SCRIPT) idVarDef( type );
	def->scope		= scope;
	def->numUsers	= 1;
	def->num		= varDefs.Append( def );

	// add the def to the list with defs with this name and set the name pointer
	AddDefToNameList( def, name );

	if ( ( type->Type() == ev_vector ) || ( ( type->Type() == ev_field ) && ( type->FieldType()->Type() == ev_vector ) ) ) {
		//
		// vector
		//
		if ( !strcmp( name, RESULT_STRING ) ) {
			// <RESULT> vector defs don't need the _x, _y and _z components
			assert( scope->Type() == ev_function );
			def->value.stackOffset	= scope->value.functionPtr->locals;
			def->initialized		= idVarDef::stackVariable;
			scope->value.functionPtr->locals += type->Size();
		} else if ( scope->TypeDef()->Inherits( &type_object ) ) {
			idTypeDef	newtype( ev_field, NULL, "float field", 0, &type_float );
			idTypeDef	*type = GetType( newtype, true );

			// set the value to the variable's position in the object
			def->value.ptrOffset = scope->TypeDef()->Size();

			// make automatic defs for the vectors elements
			// origin can be accessed as origin_x, origin_y, and origin_z
			sprintf( element, "%s_x", def->Name() );
			def_x = AllocDef( type, element, scope, constant );

			sprintf( element, "%s_y", def->Name() );
			def_y = AllocDef( type, element, scope, constant );
			def_y->value.ptrOffset = def_x->value.ptrOffset + type_float.Size();

			sprintf( element, "%s_z", def->Name() );
			def_z = AllocDef( type, element, scope, constant );
			def_z->value.ptrOffset = def_y->value.ptrOffset + type_float.Size();
		} else {
			// make automatic defs for the vectors elements
			// origin can be accessed as origin_x, origin_y, and origin_z
			sprintf( element, "%s_x", def->Name() );
			def_x = AllocDef( &type_float, element, scope, constant );

			sprintf( element, "%s_y", def->Name() );
			def_y = AllocDef( &type_float, element, scope, constant );

			sprintf( element, "%s_z", def->Name() );
			def_z = AllocDef( &type_float, element, scope, constant );

			// point the vector def to the x coordinate
			def->value			= def_x->value;
			def->initialized	= def_x->initialized;
		}
	} else if ( scope->TypeDef()->Inherits( &type_object ) ) {
		//
		// object variable
		//
		// set the value to the variable's position in the object
		def->value.ptrOffset = scope->TypeDef()->Size();
	} else if ( scope->Type() == ev_function ) {
		//
		// stack variable
		//
		// since we don't know how many local variables there are,
		// we have to have them go backwards on the stack
		def->value.stackOffset	= scope->value.functionPtr->locals;
		def->initialized		= idVarDef::stackVariable;

		if ( type->Inherits( &type_object ) ) {
			// objects only have their entity number on the stack, not the entire object
			scope->value.functionPtr->locals += type_object.Size();
		} else {
			scope->value.functionPtr->locals += type->Size();
		}
	} else {
		//
		// global variable
		//
		// [PS2_D3BFG]: Validate the range before pointer arithmetic or signed counter mutation.
		const int bytes = def->TypeDef()->Size();
		if ( bytes < 0 || bytes > MAX_GLOBALS - numVariables ) {
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
			ps2::game::script::Fail( "Exceeded global memory size (%d bytes)", MAX_GLOBALS );
#else
			throw idCompileError( va( "Exceeded global memory size (%d bytes)", MAX_GLOBALS ) );
#endif
		}
#if defined( ID_PS2 )
		// [PS2_D3BFG]: The heap storage's base; a full global space may take a zero-size def at its end.
		def->value.bytePtr = variables.Ptr() + numVariables;
#else
		def->value.bytePtr = &variables[ numVariables ];
#endif
#if ( defined( ID_PS2 ) && PS2_D3BFG_DEBUG ) || defined( ID_SCRIPT_COOK )
		CountGlobalBytes( type, name, bytes );	// [PS2_D3BFG]: Debug-only census for the cooked program image.
#endif
		numVariables += bytes;

		memset( def->value.bytePtr, 0, def->TypeDef()->Size() );
	}

	return def;
}

/*
============
idProgram::GetDef

If type is NULL, it will match any type
============
*/
idVarDef *idProgram::GetDef( const idTypeDef *type, const char *name, const idVarDef *scope ) const {
	idVarDef		*def;
	idVarDef		*bestDef;
	int				bestDepth;
	int				depth;

	bestDepth = 0;
	bestDef = NULL;
	for( def = GetDefList( name ); def != NULL; def = def->Next() ) {
		if ( def->scope->Type() == ev_namespace ) {
			depth = def->DepthOfScope( scope );
			if ( !depth ) {
				// not in the same namespace
				continue;
			}
		} else if ( def->scope != scope ) {
			// in a different function
			continue;
		} else {
			depth = 1;
		}

		if ( !bestDef || ( depth < bestDepth ) ) {
			bestDepth = depth;
			bestDef = def;
		}
	}

	// see if the name is already in use for another type
	if ( bestDef && type && ( bestDef->TypeDef() != type ) ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", va( "Type mismatch on redeclaration of %s", name ) );
#else
		throw idCompileError( va( "Type mismatch on redeclaration of %s", name ) );
#endif
	}

	return bestDef;
}

/*
============
idProgram::FreeDef
============
*/
void idProgram::FreeDef( idVarDef *def, const idVarDef *scope ) {
	idVarDef *e;
	int i;

	if ( def->Type() == ev_vector ) {
		idStr name;

		sprintf( name, "%s_x", def->Name() );
		e = GetDef( NULL, name, scope );
		if ( e ) {
			FreeDef( e, scope );
		}

		sprintf( name, "%s_y", def->Name() );
		e = GetDef( NULL, name, scope );
		if ( e ) {
			FreeDef( e, scope );
		}

		sprintf( name, "%s_z", def->Name() );
		e = GetDef( NULL, name, scope );
		if ( e ) {
			FreeDef( e, scope );
		}
	}

	varDefs.RemoveIndex( def->num );
	for( i = def->num; i < varDefs.Num(); i++ ) {
		varDefs[ i ]->num = i;
	}

	delete def;
}

/*
============
idProgram::FindFreeResultDef
============
*/
idVarDef *idProgram::FindFreeResultDef( idTypeDef *type, const char *name, idVarDef *scope, const idVarDef *a, const idVarDef *b ) {
	idVarDef *def;
	
	for( def = GetDefList( name ); def != NULL; def = def->Next() ) {
		if ( def == a || def == b ) {
			continue;
		}
		if ( def->TypeDef() != type ) {
			continue;
		}
		if ( def->scope != scope ) {
			continue;
		}
		if ( def->numUsers <= 1 ) {
			continue;
		}
		return def;
	}

	return AllocDef( type, name, scope, false );
}

/*
================
idProgram::FindFunction

Searches for the specified function in the currently loaded script.  A full namespace should be
specified if not in the global namespace.

Returns 0 if function not found.
Returns >0 if function found.
================
*/
function_t *idProgram::FindFunction( const char *name ) const {
	int			start;
	int			pos;
	idVarDef	*namespaceDef;
	idVarDef	*def;

	assert( name );

	idStr fullname = name;
	start = 0;
	namespaceDef = &def_namespace;
	do {
		pos = fullname.Find( "::", true, start );
		if ( pos < 0 ) {
			break;
		}

		idStr namespaceName = fullname.Mid( start, pos - start );
		def = GetDef( NULL, namespaceName, namespaceDef );
		if ( !def ) {
			// couldn't find namespace
			return NULL;
		}
		namespaceDef = def;

		// skip past the ::
		start = pos + 2;
	} while( def->Type() == ev_namespace );

	idStr funcName = fullname.Right( fullname.Length() - start );
	def = GetDef( NULL, funcName, namespaceDef );
	if ( !def ) {
		// couldn't find function
		return NULL;
	}

	if ( ( def->Type() == ev_function ) && ( def->value.functionPtr->eventdef == NULL ) ) {
		return def->value.functionPtr;
	}

	// is not a function, or is an eventdef
	return NULL;
}

/*
================
idProgram::FindFunction

Searches for the specified object function in the currently loaded script.

Returns 0 if function not found.
Returns >0 if function found.
================
*/
function_t *idProgram::FindFunction( const char *name, const idTypeDef *type ) const {
	const idVarDef	*tdef;
	const idVarDef	*def;

	// look for the function
	def = NULL;
	for( tdef = type->def; tdef != &def_object; tdef = tdef->TypeDef()->SuperClass()->def ) {
		def = GetDef( NULL, name, tdef );
		if ( def ) {
			return def->value.functionPtr;
		}
	}

	return NULL;
}

/*
================
idProgram::AllocFunction
================
*/
function_t &idProgram::AllocFunction( idVarDef *def ) {
	if ( functions.Num() >= functions.Max() ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", va( "Exceeded maximum allowed number of functions (%d)", functions.Max() ) );
#else
		throw idCompileError( va( "Exceeded maximum allowed number of functions (%d)", functions.Max() ) );
#endif
	}

	// fill in the dfunction
	function_t &func	= *functions.Alloc();
	func.eventdef		= NULL;
	func.def			= def;
	func.type			= def->TypeDef();
	func.firstStatement	= 0;
	func.numStatements	= 0;
	func.parmTotal		= 0;
	func.locals			= 0;
	func.filenum		= filenum;
	func.parmSize.SetGranularity( 1 );
	func.SetName( def->GlobalName() );

	def->SetFunction( &func );

	return func;
}

/*
================
idProgram::SetEntity
================
*/
void idProgram::SetEntity( const char *name, idEntity *ent ) {
	idVarDef	*def;
	idStr		defName( "$" );

	defName += name;

	def = GetDef( &type_entity, defName, &def_namespace );
	if ( def != NULL && ( def->initialized != idVarDef::stackVariable ) ) {
		// 0 is reserved for NULL entity
		if ( !ent ) {
			*def->value.entityNumberPtr = 0;
		} else {
			*def->value.entityNumberPtr = ent->entityNumber + 1;
		}
	}
}

/*
================
idProgram::AllocStatement
================
*/
statement_t *idProgram::AllocStatement() {
	if ( statements.Num() >= statements.Max() ) {
		// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
		ps2::game::script::Fail( "%s", va( "Exceeded maximum allowed number of statements (%d)", statements.Max() ) );
#else
		throw idCompileError( va( "Exceeded maximum allowed number of statements (%d)", statements.Max() ) );
#endif
	}
	return statements.Alloc();
}

/*
==============
idProgram::BeginCompilation

called before compiling a batch of files, clears the pr struct
==============
*/
void idProgram::BeginCompilation() {
	// [PS2_D3BFG]: Failed compilation is fatal, including console snippets; discard partial state.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
	ps2::game::script::ErrorScope cleanup( []( void *data ) {
		static_cast<idProgram *>( data )->FreeData();
	}, this );
#endif
	statement_t	*statement;

	FreeData();

#if defined( ID_PS2 )
	// [PS2_D3BFG]: Allocate the fixed program storage zeroed on the TAG_SCRIPT heap; FreeData releases it.
	variables.Allocate( TAG_SCRIPT );
	variables.SetNum( MAX_GLOBALS );
	variableDefaults.Allocate( TAG_SCRIPT );
	functions.Allocate( TAG_SCRIPT );
	statements.Allocate( TAG_SCRIPT );
#endif
#if ( defined( ID_PS2 ) && PS2_D3BFG_DEBUG ) || defined( ID_SCRIPT_COOK )
	memset( s_scriptGlobalBytes, 0, sizeof( s_scriptGlobalBytes ) );	// [PS2_D3BFG]: Debug-only census.
	memset( s_scriptGlobalDefs, 0, sizeof( s_scriptGlobalDefs ) );
#endif

	// [PS2_D3BFG]: Desktop keeps recoverable exceptions; target errors use explicit fatal cleanup.
#if !defined( ID_PS2 ) && !defined( ID_HOST_TEST )
	try
#endif
	{
		// make the first statement a return for a "NULL" function
		statement = AllocStatement();
		statement->linenumber	= 0;
		statement->file 		= 0;
		statement->op			= OP_RETURN;
		statement->a			= NULL;
		statement->b			= NULL;
		statement->c			= NULL;

		// define NULL
		//AllocDef( &type_void, "<NULL>", &def_namespace, true );

		// define the return def
		returnDef = AllocDef( &type_vector, "<RETURN>", &def_namespace, false );

		// define the return def for strings
		returnStringDef = AllocDef( &type_string, "<RETURN>", &def_namespace, false );

		// define the sys object
		sysDef = AllocDef( &type_void, "sys", &def_namespace, true );
	}

#if !defined( ID_PS2 ) && !defined( ID_HOST_TEST )
	catch( idCompileError &err ) {
		gameLocal.Error( "%s", err.GetError() );
	}
#endif
}

/*
==============
idProgram::DisassembleStatement
==============
*/
void idProgram::DisassembleStatement( idFile *file, int instructionPointer ) const {
	opcode_t			*op;
	const statement_t	*statement;

	statement = &statements[ instructionPointer ];
	op = &idCompiler::opcodes[ statement->op ];
	file->Printf( "%20s(%d):\t%6d: %15s\t", fileList[ statement->file ].c_str(), statement->linenumber, instructionPointer, op->opname );

	if ( statement->a ) {
		file->Printf( "\ta: " );
		statement->a->PrintInfo( file, instructionPointer );
	}

	if ( statement->b ) {
		file->Printf( "\tb: " );
		statement->b->PrintInfo( file, instructionPointer );
	}

	if ( statement->c ) {
		file->Printf( "\tc: " );
		statement->c->PrintInfo( file, instructionPointer );
	}

	file->Printf( "\n" );
}

/*
==============
idProgram::Disassemble
==============
*/
void idProgram::Disassemble() const {
	int					i;
	int					instructionPointer;
	const function_t	*func;
	idFile				*file;

	file = fileSystem->OpenFileByMode( "script/disasm.txt", FS_WRITE );

	for( i = 0; i < functions.Num(); i++ ) {
		func = &functions[ i ];
		if ( func->eventdef ) {
			// skip eventdefs
			continue;
		}

		file->Printf( "\nfunction %s() %d stack used, %d parms, %d locals {\n", func->Name(), func->locals, func->parmTotal, func->locals - func->parmTotal );

		for( instructionPointer = 0; instructionPointer < func->numStatements; instructionPointer++ ) {
			DisassembleStatement( file, func->firstStatement + instructionPointer );
		}
	
		file->Printf( "}\n" );
	}

	fileSystem->CloseFile( file );
}

/*
==============
idProgram::FinishCompilation

Called after all files are compiled to check for errors
==============
*/
void idProgram::FinishCompilation() {
	int	i;

	top_functions	= functions.Num();
	top_statements	= statements.Num();
	top_types		= types.Num();
	top_defs		= varDefs.Num();
	top_files		= fileList.Num();

	variableDefaults.Clear();
	variableDefaults.SetNum( numVariables );

	for( i = 0; i < numVariables; i++ ) {
		variableDefaults[ i ] = variables[ i ];
	}
}

/*
==============
idProgram::CompileStats

called after all files are compiled to report memory usage.
==============
*/
// [PS2_D3BFG]: Compile reporting needs only Common, not a resident game instance.
void idProgram::CompileStats() {
	int	memused;
	int	memallocated;
	int	numdefs;
	int	stringspace;
	int funcMem;
	int	i;

	common->Printf( "---------- Compile stats ----------\n" );
	DEV_PRINT( common->DPrintf( "Files loaded:\n" ) );	// [PS2_D3BFG]

	stringspace = 0;
	for( i = 0; i < fileList.Num(); i++ ) {
		DEV_PRINT( common->DPrintf( "   %s\n", fileList[ i ].c_str() ) );	// [PS2_D3BFG]
		stringspace += fileList[ i ].Allocated();
	}
	stringspace += fileList.Size();

	numdefs = varDefs.Num();
	memused = varDefs.Num() * sizeof( idVarDef );
	memused += types.Num() * sizeof( idTypeDef );
	memused += stringspace;

	for( i = 0; i < types.Num(); i++ ) {
		memused += types[ i ]->Allocated();
	}

	funcMem = functions.MemoryUsed();
	for( i = 0; i < functions.Num(); i++ ) {
		funcMem += functions[ i ].Allocated();
	}

	memallocated = funcMem + memused + sizeof( idProgram );

	memused += statements.MemoryUsed();
	memused += functions.MemoryUsed();	// name and filename of functions are shared, so no need to include them
	memused += MAX_GLOBALS;	// [PS2_D3BFG]: The heap storage's size; sizeof( variables ) is the list object's on the target.

	common->Printf( "\nMemory usage:\n" );
	common->Printf( "     Strings: %d, %d bytes\n", fileList.Num(), stringspace );
	common->Printf( "  Statements: %d, %d bytes\n", statements.Num(), statements.MemoryUsed() );
	common->Printf( "   Functions: %d, %d bytes\n", functions.Num(), funcMem );
	common->Printf( "   Variables: %d bytes\n", numVariables );
	common->Printf( "    Mem used: %d bytes\n", memused );
	common->Printf( " Static data: %d bytes\n", sizeof( idProgram ) );
	common->Printf( "   Allocated: %d bytes\n", memallocated );
	common->Printf( " Thread size: %d bytes\n\n", sizeof( idThread ) );

#if ( defined( ID_PS2 ) && PS2_D3BFG_DEBUG ) || defined( ID_SCRIPT_COOK )
	CompileCensus();	// [PS2_D3BFG]: Debug-only census for the cooked program image.
#endif
}

#if ( defined( ID_PS2 ) && PS2_D3BFG_DEBUG ) || defined( ID_SCRIPT_COOK )
/*
==============
idProgram::CompileCensus

[PS2_D3BFG]: Counts what the compiled program holds and what its statements reference, so the
cooked program image can be sized from facts. Debug builds only.
==============
*/
void idProgram::CompileCensus() const {
	int i, j;

	common->Printf( "---------- Script census ----------\n" );

	// global bytes, by what reserved them
	for ( i = 0; i < SCRIPT_GLOBAL_USES; i++ ) {
		for ( j = 0; j < ev_boolean + 2; j++ ) {
			if ( s_scriptGlobalDefs[ i ][ j ] != 0 ) {
				common->Printf( "[D3BFG] SCRIPT_CENSUS globals use=\"%s\" type=%s defs=%d bytes=%d\n", s_scriptGlobalUseNames[ i ], s_scriptTypeNames[ j ], s_scriptGlobalDefs[ i ][ j ], s_scriptGlobalBytes[ i ][ j ] );
			}
		}
	}

	// defs by storage class and scope
	int byInitialized[ 4 ] = {};
	int byScope[ ev_boolean + 2 ] = {};
	int results = 0;
	int immediates = 0;
	int components = 0;
	int entityGlobals = 0;
	int functionDefs = 0;
	for ( i = 0; i < varDefs.Num(); i++ ) {
		const idVarDef *def = varDefs[ i ];
		byInitialized[ def->initialized ]++;
		byScope[ ( def->scope != NULL ) ? def->scope->Type() + 1 : 0 ]++;
		const char *name = def->Name();
		const int length = idStr::Length( name );
		if ( idStr::Cmp( name, RESULT_STRING ) == 0 ) {
			results++;
		} else if ( idStr::Cmpn( name, "<IMMEDIATE>", 11 ) == 0 ) {
			immediates++;
		}
		if ( length > 2 && name[ length - 2 ] == '_' && ( name[ length - 1 ] == 'x' || name[ length - 1 ] == 'y' || name[ length - 1 ] == 'z' ) ) {
			components++;
		}
		if ( name[ 0 ] == '$' ) {
			entityGlobals++;
		}
		if ( def->Type() == ev_function && def->initialized != idVarDef::stackVariable ) {
			functionDefs++;
		}
	}
	common->Printf( "[D3BFG] SCRIPT_CENSUS defs total=%d names=%d uninitialized=%d variable=%d constant=%d stack=%d results=%d immediates=%d components=%d entity_globals=%d function_defs=%d\n",
		varDefs.Num(), varDefNames.Num(), byInitialized[ 0 ], byInitialized[ 1 ], byInitialized[ 2 ], byInitialized[ 3 ], results, immediates, components, entityGlobals, functionDefs );
	for ( j = 0; j < ev_boolean + 2; j++ ) {
		if ( byScope[ j ] != 0 ) {
			common->Printf( "[D3BFG] SCRIPT_CENSUS defs scope=%s count=%d\n", s_scriptTypeNames[ j ], byScope[ j ] );
		}
	}

	// defs the statements reference: the operand table of a cooked image
	byte *referenced = static_cast<byte *>( Mem_ClearedAlloc( varDefs.Num(), TAG_TEMP ) );
	int maxNum = -1;
	int operandSlots = 0;
	for ( i = 0; i < statements.Num(); i++ ) {
		const statement_t &st = statements[ i ];
		const idVarDef *operands[ 3 ] = { st.a, st.b, st.c };
		for ( j = 0; j < 3; j++ ) {
			if ( operands[ j ] != NULL ) {
				operandSlots++;
				if ( operands[ j ]->num >= 0 && operands[ j ]->num < varDefs.Num() ) {
					referenced[ operands[ j ]->num ] = 1;
					maxNum = Max( maxNum, operands[ j ]->num );
				}
			}
		}
	}
	int referencedDefs = 0;
	int referencedByInitialized[ 4 ] = {};
	int referencedByType[ ev_boolean + 2 ] = {};
	for ( i = 0; i < varDefs.Num(); i++ ) {
		if ( referenced[ i ] ) {
			referencedDefs++;
			referencedByInitialized[ varDefs[ i ]->initialized ]++;
			referencedByType[ varDefs[ i ]->Type() + 1 ]++;
		}
	}
	Mem_Free( referenced );
	common->Printf( "[D3BFG] SCRIPT_CENSUS operands referenced=%d max_num=%d slots=%d uninitialized=%d variable=%d constant=%d stack=%d\n",
		referencedDefs, maxNum, operandSlots, referencedByInitialized[ 0 ], referencedByInitialized[ 1 ], referencedByInitialized[ 2 ], referencedByInitialized[ 3 ] );
	for ( j = 0; j < ev_boolean + 2; j++ ) {
		if ( referencedByType[ j ] != 0 ) {
			common->Printf( "[D3BFG] SCRIPT_CENSUS operands type=%s count=%d\n", s_scriptTypeNames[ j ], referencedByType[ j ] );
		}
	}

	// types, and the names runtime lookups reach through them
	int typesByKind[ ev_boolean + 2 ] = {};
	int objectFields = 0;
	int objectFieldNameBytes = 0;
	int virtualFunctions = 0;
	int functionParms = 0;
	for ( i = 0; i < types.Num(); i++ ) {
		const idTypeDef *type = types[ i ];
		typesByKind[ type->Type() + 1 ]++;
		if ( type->Type() == ev_object ) {
			objectFields += type->NumParameters();
			for ( j = 0; j < type->NumParameters(); j++ ) {
				objectFieldNameBytes += idStr::Length( type->GetParmName( j ) ) + 1;
			}
			virtualFunctions += type->NumFunctions();
		} else if ( type->Type() == ev_function ) {
			functionParms += type->NumParameters();
		}
	}
	for ( j = 0; j < ev_boolean + 2; j++ ) {
		if ( typesByKind[ j ] != 0 ) {
			common->Printf( "[D3BFG] SCRIPT_CENSUS types kind=%s count=%d\n", s_scriptTypeNames[ j ], typesByKind[ j ] );
		}
	}
	common->Printf( "[D3BFG] SCRIPT_CENSUS types total=%d object_fields=%d object_field_name_bytes=%d virtual_functions=%d function_type_parms=%d\n",
		types.Num(), objectFields, objectFieldNameBytes, virtualFunctions, functionParms );

	// functions
	int events = 0;
	int parmSizes = 0;
	int nameBytes = 0;
	int maxStatements = 0;
	int maxLocals = 0;
	for ( i = 0; i < functions.Num(); i++ ) {
		const function_t &func = functions[ i ];
		if ( func.eventdef != NULL ) {
			events++;
		}
		parmSizes += func.parmSize.Num();
		nameBytes += idStr::Length( func.Name() ) + 1;
		maxStatements = Max( maxStatements, func.numStatements );
		maxLocals = Max( maxLocals, func.locals );
	}
	int defNameBytes = 0;
	for ( i = 0; i < varDefNames.Num(); i++ ) {
		defNameBytes += idStr::Length( varDefNames[ i ]->Name() ) + 1;
	}
	common->Printf( "[D3BFG] SCRIPT_CENSUS functions total=%d events=%d parm_sizes=%d name_bytes=%d max_statements=%d max_locals=%d def_name_bytes=%d\n",
		functions.Num(), events, parmSizes, nameBytes, maxStatements, maxLocals, defNameBytes );
}
#endif

/*
================
idProgram::CompileText
================
*/
bool idProgram::CompileText( const char *source, const char *text, bool console ) {
	// [PS2_D3BFG]: Failed compilation is fatal, including console snippets; discard partial state.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
	ps2::game::script::ErrorScope cleanup( []( void *data ) {
		static_cast<idProgram *>( data )->FreeData();
	}, this );
#endif
	// [PS2_D3BFG]: The compiler depends on event queries, not the whole game singleton.
	idCompiler compiler( *this, idEventDef::FindEvent,
		[]( const idEventDef &event ) { return idThread::Type.RespondsTo( event ); } );
	int			i;
	idVarDef	*def;
	idStr		ospath;

	// use a full os path for GetFilenum since it calls OSPathToRelativePath to convert filenames from the parser
	ospath = fileSystem->RelativePathToOSPath( source );
	filenum = GetFilenum( ospath );

	// [PS2_D3BFG]: Desktop keeps recoverable exceptions; target errors use explicit fatal cleanup.
#if !defined( ID_PS2 ) && !defined( ID_HOST_TEST )
	try
#endif
	{
		// [PS2_D3BFG]: The source argument identifies this input; filename can be empty/stale.
		compiler.CompileFile( text, source, console );

		// check to make sure all functions prototyped have code
		for( i = 0; i < varDefs.Num(); i++ ) {
			def = varDefs[ i ];
			if ( ( def->Type() == ev_function ) && ( ( def->scope->Type() == ev_namespace ) || def->scope->TypeDef()->Inherits( &type_object ) ) ) {
				if ( !def->value.functionPtr->eventdef && !def->value.functionPtr->firstStatement ) {
					// [PS2_D3BFG]: No exception unwinding on the target.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
					ps2::game::script::Fail( "%s", va( "function %s was not defined\n", def->GlobalName() ) );
#else
					throw idCompileError( va( "function %s was not defined\n", def->GlobalName() ) );
#endif
				}
			}
		}
	}
	
#if !defined( ID_PS2 ) && !defined( ID_HOST_TEST )
	catch( idCompileError &err ) {
		if ( console ) {
			gameLocal.Printf( "%s\n", err.GetError() );
			return false;
		} else {
			gameLocal.Error( "%s\n", err.GetError() );
		}
	};
#endif

	if ( !console ) {
		CompileStats();
	}

	return true;
}

/*
================
idProgram::CompileFunction
================
*/
const function_t *idProgram::CompileFunction( const char *functionName, const char *text ) {
	bool result;

	result = CompileText( functionName, text, false );

#if !defined( ID_PS2 ) || PS2_D3BFG_DEBUG	// [PS2_D3BFG]: Debug visualization is debug-only on the PS2.
	if ( g_disasm.GetBool() ) {
		Disassemble();
	}
#endif

	if ( !result ) {
		gameLocal.Error( "Compile failed." );
	}

	return FindFunction( functionName );
}

/*
================
idProgram::CompileFile
================
*/
void idProgram::CompileFile( const char *filename ) {
	char *src;
	bool result;

	if ( fileSystem->ReadFile( filename, ( void ** )&src, NULL ) < 0 ) {
		gameLocal.Error( "Couldn't load %s\n", filename );
	}

	// [PS2_D3BFG]: File ownership must be released even when compilation terminates.
#if defined( ID_PS2 ) || defined( ID_HOST_TEST )
	ps2::game::script::ErrorScope sourceCleanup( []( void *data ) { fileSystem->FreeFile( data ); }, src );
#endif
	result = CompileText( filename, src, false );

	fileSystem->FreeFile( src );

#if !defined( ID_PS2 ) || PS2_D3BFG_DEBUG	// [PS2_D3BFG]: Debug visualization is debug-only on the PS2.
	if ( g_disasm.GetBool() ) {
		Disassemble();
	}
#endif

	if ( !result ) {
		gameLocal.Error( "Compile failed in file %s.", filename );
	}
}

/*
================
idProgram::FreeData
================
*/
void idProgram::FreeData() {
	int i;

	// free the defs
	varDefs.DeleteContents( true );
	varDefNames.DeleteContents( true );
	varDefNameHash.Free();

	returnDef		= NULL;
	returnStringDef = NULL;
	sysDef			= NULL;

	// free any special types we've created
	types.DeleteContents( true );
	typesHash.Free();

	filenum = 0;

	numVariables = 0;
	// [PS2_D3BFG]: The heap storage is released below; BeginCompilation allocates it zeroed again.
#if !defined( ID_PS2 )
	memset( variables, 0, sizeof( variables ) );
#endif

	// clear all the strings in the functions so that it doesn't look like we're leaking memory.
	for( i = 0; i < functions.Num(); i++ ) {
		functions[ i ].Clear();
	}

	filename.Clear();
	fileList.Clear();
	statements.Clear();
	functions.Clear();
#if defined( ID_PS2 )
	variables.Release();
	variableDefaults.Release();
	functions.Release();
	statements.Release();
#endif

	top_functions	= 0;
	top_statements	= 0;
	top_types		= 0;
	top_defs		= 0;
	top_files		= 0;

	filename = "";
}

/*
================
idProgram::Startup
================
*/
void idProgram::Startup( const char *defaultScript ) {
	gameLocal.Printf( "Initializing scripts\n" );

	// make sure all data is freed up
	idThread::Restart();

	// get ready for loading scripts
	BeginCompilation();

	// load the default script
	if ( defaultScript && *defaultScript ) {
		CompileFile( defaultScript );
	}

	FinishCompilation();

#if defined( ID_SCRIPT_CHECK )
	// [PS2_D3BFG]: The cook's equivalence check: log the image the cook's writer makes of this compile.
	ps2::game::script::ReportCompiledImage( *this );
#endif
}

/*
================
idProgram::Save
================
*/
void idProgram::Save( idSaveGame *savefile ) const {
	int i;
	int currentFileNum = top_files;

	savefile->WriteInt( (fileList.Num() - currentFileNum) );
	while ( currentFileNum < fileList.Num() ) {
		savefile->WriteString( fileList[ currentFileNum ] );
		currentFileNum++;
	}

	for ( i = 0; i < variableDefaults.Num(); i++ ) {
		if ( variables[i] != variableDefaults[i] ) {
			savefile->WriteInt( i );
			savefile->WriteByte( variables[i] );
		}
	}
	// Mark the end of the diff with default variables with -1
	savefile->WriteInt( -1 );

	savefile->WriteInt( numVariables );
	for ( i = variableDefaults.Num(); i < numVariables; i++ ) {
		savefile->WriteByte( variables[i] );
	}

	int checksum = CalculateChecksum();
	savefile->WriteInt( checksum );
}

/*
================
idProgram::Restore
================
*/
bool idProgram::Restore( idRestoreGame *savefile ) {
	int i, num, index;
	bool result = true;
	idStr scriptname;

	savefile->ReadInt( num );
	for ( i = 0; i < num; i++ ) {
		savefile->ReadString( scriptname );
		CompileFile( scriptname );
	}

	savefile->ReadInt( index );
	while( index >= 0 ) {
		savefile->ReadByte( variables[index] );
		savefile->ReadInt( index );
	}

	savefile->ReadInt( num );
	for ( i = variableDefaults.Num(); i < num; i++ ) {
		savefile->ReadByte( variables[i] );
	}

	int saved_checksum, checksum;

	savefile->ReadInt( saved_checksum );
	checksum = CalculateChecksum();

	if ( saved_checksum != checksum ) {
		result = false;
	}

	return result;
}

/*
================
idProgram::CalculateChecksum
================
*/
int idProgram::CalculateChecksum() const {
	int i, result;

	typedef struct {
		unsigned short	op;
		int				a;
		int				b;
		int				c;
		unsigned short	linenumber;
		unsigned short	file;
	} statementBlock_t;

	statementBlock_t	*statementList = new (TAG_SCRIPT) statementBlock_t[ statements.Num() ];

	memset( statementList, 0, ( sizeof(statementBlock_t) * statements.Num() ) );

	// Copy info into new list, using the variable numbers instead of a pointer to the variable
	for( i = 0; i < statements.Num(); i++ ) {
		statementList[i].op = statements[i].op;

		if ( statements[i].a ) {
			statementList[i].a = statements[i].a->num;
		} else {
			statementList[i].a = -1;
		}
		if ( statements[i].b ) {
			statementList[i].b = statements[i].b->num;
		} else {
			statementList[i].b = -1;
		}
		if ( statements[i].c ) {
			statementList[i].c = statements[i].c->num;
		} else {
			statementList[i].c = -1;
		}

		statementList[i].linenumber = statements[i].linenumber;
		statementList[i].file = statements[i].file;
	}

	result = MD4_BlockChecksum( statementList, ( sizeof(statementBlock_t) * statements.Num() ) );

	delete [] statementList;

	return result;
}

/*
==============
idProgram::Restart

Restores all variables to their initial value
==============
*/
void idProgram::Restart() {
	int i;

	idThread::Restart();

	//
	// since there may have been a script loaded by the map or the user may
	// have typed "script" from the console, free up any types and vardefs that
	// have been allocated after the initial startup
	//
	for( i = top_types; i < types.Num(); i++ ) {
		delete types[ i ];
	}
	types.SetNum( top_types );

	typesHash.Free();
	for( i = 0; i < types.Num(); i++ ) {
		typesHash.Add( idStr::Hash( types[i]->Name() ), i );
	}

	for( i = top_defs; i < varDefs.Num(); i++ ) {
		delete varDefs[ i ];
	}
	varDefs.SetNum( top_defs );

	for( i = top_functions; i < functions.Num(); i++ ) {
		functions[ i ].Clear();
	}
	functions.SetNum( top_functions	);

	statements.SetNum( top_statements );
	fileList.SetNum( top_files );
	filename.Clear();
	
	// reset the variables to their default values
	numVariables = variableDefaults.Num();
	for( i = 0; i < numVariables; i++ ) {
		variables[ i ] = variableDefaults[ i ];
	}
}

/*
================
idProgram::GetFilenum
================
*/
int idProgram::GetFilenum( const char *name ) {
	if ( filename == name ) {
		return filenum;
	}

	idStr strippedName;
	strippedName = fileSystem->OSPathToRelativePath( name );
	if ( !strippedName.Length() ) {
		// not off the base path so just use the full path
		filenum = fileList.AddUnique( name );
	} else {
		filenum = fileList.AddUnique( strippedName );
	}

	// save the unstripped name so that we don't have to strip the incoming name every time we call GetFilenum
	filename = name;

	return filenum;
}

/*
================
idProgram::idProgram
================
*/
idProgram::idProgram() {
	varDefs.SetGranularity( 256 );
	varDefNames.SetGranularity( 256 );

	FreeData();
}

/*
================
idProgram::~idProgram
================
*/
idProgram::~idProgram() {
	FreeData();
}

/*
================
idProgram::ReturnEntity
================
*/
void idProgram::ReturnEntity( idEntity *ent ) {
	if ( ent ) {
		*returnDef->value.entityNumberPtr = ent->entityNumber + 1;
	} else {
		*returnDef->value.entityNumberPtr = 0;
	}
}

#endif // !defined( ID_SCRIPT_IMAGE )

#if defined( ID_SCRIPT_IMAGE )

/***********************************************************************

  [PS2_D3BFG]: The cooked program

  The host script cook compiled the program; ps2/game/script_image.cpp loads and checks it. These
  are the engine's runtime views of its records, with id's lookup rules.

***********************************************************************/

static idTypeDef * const s_builtinTypes[ ps2::game::script::image::kBuiltinTypes ] = {
	&type_void, &type_scriptevent, &type_namespace, &type_string, &type_float, &type_vector, &type_entity, &type_field,
	&type_function, &type_virtualfunction, &type_pointer, &type_object, &type_jumpoffset, &type_argsize, &type_boolean
};

idTypeDef * const *idVarDef::typeTable = s_builtinTypes;
idVarDef *scriptOperand_t::defs = NULL;

static_assert( sizeof( idVarDef ) == sizeof( ps2::game::script::image::Def ), "an image def is the engine's idVarDef" );
static_assert( sizeof( statement_t ) == sizeof( ps2::game::script::image::Statement ), "an image statement is the engine's statement_t" );
static_assert( ev_boolean + 1 == ps2::game::script::image::kBuiltinTypes, "one built-in type per etype" );

/*
================
idTypeDef::idTypeDef
================
*/
idTypeDef::idTypeDef() {
	type			= ev_void;
	name			= "";
	size			= 0;
	auxType			= NULL;
	parmTypes		= NULL;
	parmNames		= NULL;
	functions		= NULL;
	numParms		= 0;
	numFunctions	= 0;
	def				= NULL;
}

idTypeDef::idTypeDef( etype_t etype, idVarDef *edef, const char *ename, int esize, idTypeDef *aux ) {
	type			= etype;
	name			= ename;
	size			= esize;
	auxType			= aux;
	parmTypes		= NULL;
	parmNames		= NULL;
	functions		= NULL;
	numParms		= 0;
	numFunctions	= 0;
	def				= edef;
}

bool idTypeDef::Inherits( const idTypeDef *basetype ) const {
	if ( type != ev_object ) {
		return false;
	}

	if ( this == basetype ) {
		return true;
	}

	for( const idTypeDef *superType = auxType; superType != NULL; superType = superType->auxType ) {
		if ( superType == basetype ) {
			return true;
		}
	}

	return false;
}

const char *idTypeDef::Name() const {
	return name;
}

etype_t idTypeDef::Type() const {
	return type;
}

int idTypeDef::Size() const {
	return size;
}

idTypeDef *idTypeDef::SuperClass() const {
	if ( type != ev_object ) {
		gameLocal.Error( "idTypeDef::SuperClass : tried to get superclass of a non-object type" );
	}

	return auxType;
}

idTypeDef *idTypeDef::ReturnType() const {
	if ( type != ev_function ) {
		gameLocal.Error( "idTypeDef::ReturnType: tried to get return type on non-function type" );
	}

	return auxType;
}

idTypeDef *idTypeDef::FieldType() const {
	if ( type != ev_field ) {
		gameLocal.Error( "idTypeDef::FieldType: tried to get field type on non-field type" );
	}

	return auxType;
}

idTypeDef *idTypeDef::PointerType() const {
	if ( type != ev_pointer ) {
		gameLocal.Error( "idTypeDef::PointerType: tried to get pointer type on non-pointer" );
	}

	return auxType;
}

int idTypeDef::NumParameters() const {
	return numParms;
}

idTypeDef *idTypeDef::GetParmType( int parmNumber ) const {
	assert( parmNumber >= 0 );
	assert( parmNumber < numParms );
	return parmTypes[ parmNumber ];
}

const char *idTypeDef::GetParmName( int parmNumber ) const {
	assert( parmNumber >= 0 );
	assert( parmNumber < numParms );
	// Function types are shared by signature and keep no parameter names.
	return parmNames != NULL ? parmNames[ parmNumber ] : "";
}

int idTypeDef::NumFunctions() const {
	return numFunctions;
}

int idTypeDef::GetFunctionNumber( const function_t *func ) const {
	for( int i = 0; i < numFunctions; i++ ) {
		if ( functions[ i ] == func ) {
			return i;
		}
	}
	return -1;
}

const function_t *idTypeDef::GetFunction( int funcNumber ) const {
	assert( funcNumber >= 0 );
	assert( funcNumber < numFunctions );
	return functions[ funcNumber ];
}

/*
================
idVarDef::idVarDef

The built-in defs (def_void ...); the image's come from the loader.
================
*/
idVarDef::idVarDef( idTypeDef *typeptr ) {
	value.intPtr	= NULL;
	initialized		= uninitialized;
	valueKind		= static_cast<unsigned char>( ps2::game::script::image::ValueKind::None );
	typeIndex		= ( typeptr != NULL ) ? static_cast<unsigned short>( typeptr->Type() ) : 0;
}

/*
================
idVarDef::Name

Diagnostics only: the name a lookup reaches this def by, if any.
================
*/
const char *idVarDef::Name() const {
	return gameLocal.program.DefName( this );
}

/*
================
idProgram::idProgram
================
*/
idProgram::idProgram() {
	image				= NULL;
	objects				= NULL;
	debugInfo			= NULL;
	fileNames			= NULL;
	variables			= NULL;
	returnDef			= NULL;
	returnStringDef		= NULL;
	FreeData();
}

/*
================
idProgram::~idProgram
================
*/
idProgram::~idProgram() {
	FreeData();
}

/*
================
idProgram::FreeData
================
*/
void idProgram::FreeData() {
	Mem_Free( image );
	Mem_Free( objects );
	Mem_Free( debugInfo );
	Mem_Free( fileNames );
	Mem_Free( variables );

	image				= NULL;
	objects				= NULL;
	debugInfo			= NULL;
	statements			= NULL;
	numStatements		= 0;
	defs				= NULL;
	numDefs				= 0;
	lookups				= NULL;
	numLookups			= 0;
	scopes				= NULL;
	numScopes			= 0;
	strings				= NULL;
	functions			= NULL;
	numFunctions		= 0;
	typeTable			= NULL;
	numTypes			= 0;
	variables			= NULL;
	variableDefaults	= NULL;
	numVariables		= 0;
	checksum			= 0;
	lines				= NULL;
	fileRuns			= NULL;
	numFileRuns			= 0;
	fileNames			= NULL;
	numFiles			= 0;
	returnDef			= NULL;
	returnStringDef		= NULL;

	scriptOperand_t::defs	= NULL;
	idVarDef::typeTable		= s_builtinTypes;
}

/*
================
idProgram::Startup
================
*/
void idProgram::Startup( const char *defaultScript ) {
	gameLocal.Printf( "Initializing scripts\n" );

	// make sure all data is freed up
	idThread::Restart();
	FreeData();

	idStr imageName = defaultScript;
	imageName.SetFileExtension( ".d3sp" );

	idStr error;
	if ( !ps2::game::script::ImageLoader::Load( *this, imageName, error ) ) {
		FreeData();
		common->FatalError( "Couldn't load the cooked script %s: %s", imageName.c_str(), error.c_str() );
	}

	gameLocal.Printf( "Loaded '%s': %d statements, %d functions, %d defs, %d types, %d global bytes\n", imageName.c_str(),
		numStatements, numFunctions, numDefs, numTypes - static_cast<int>( ps2::game::script::image::kBuiltinTypes ), numVariables );
}

/*
================
idProgram::Restart

Restores all variables to their initial value.
================
*/
void idProgram::Restart() {
	idThread::Restart();

	if ( numVariables > 0 ) {
		memcpy( variables, variableDefaults, numVariables );
	}
}

/*
================
idProgram::Save
================
*/
void idProgram::Save( idSaveGame *savefile ) const {
	// no map scripts are compiled in on the PS2
	savefile->WriteInt( 0 );

	for ( int i = 0; i < numVariables; i++ ) {
		if ( variables[i] != variableDefaults[i] ) {
			savefile->WriteInt( i );
			savefile->WriteByte( variables[i] );
		}
	}
	// Mark the end of the diff with default variables with -1
	savefile->WriteInt( -1 );

	savefile->WriteInt( numVariables );
	savefile->WriteInt( CalculateChecksum() );
}

/*
================
idProgram::Restore
================
*/
bool idProgram::Restore( idRestoreGame *savefile ) {
	int num, index;

	savefile->ReadInt( num );
	if ( num != 0 ) {
		savefile->Error( "idProgram::Restore: the save game compiled map scripts, which the PS2 does not run" );
	}

	savefile->ReadInt( index );
	while( index >= 0 ) {
		if ( index >= numVariables ) {
			savefile->Error( "idProgram::Restore: global variable %d is out of range", index );
		}
		savefile->ReadByte( variables[index] );
		savefile->ReadInt( index );
	}

	savefile->ReadInt( num );
	if ( num != numVariables ) {
		return false;
	}

	int saved_checksum;
	savefile->ReadInt( saved_checksum );
	return saved_checksum == CalculateChecksum();
}

/*
================
idProgram::CalculateChecksum

The cooked image's own checksum identifies the program.
================
*/
int idProgram::CalculateChecksum() const {
	return checksum;
}

/*
================
idProgram::GetFilename
================
*/
const char *idProgram::GetFilename( int num ) {
	if ( fileNames == NULL || num < 0 || num >= numFiles ) {
		return "";
	}

	return fileNames[ num ];
}

/*
================
idProgram::GetLineNumberForStatement
================
*/
int idProgram::GetLineNumberForStatement( int index ) {
	if ( lines == NULL || index < 0 || index >= numStatements ) {
		return 0;
	}

	return lines[ index ];
}

/*
================
idProgram::GetFilenameForStatement
================
*/
const char *idProgram::GetFilenameForStatement( int index ) {
	if ( fileRuns == NULL || index < 0 || index >= numStatements ) {
		return "";
	}

	// the last run that starts at or before the statement
	int low = 0;
	int high = numFileRuns - 1;
	while ( low < high ) {
		const int middle = ( low + high + 1 ) / 2;
		if ( static_cast<int>( fileRuns[ middle ].firstStatement ) <= index ) {
			low = middle;
		} else {
			high = middle - 1;
		}
	}

	return GetFilename( static_cast<int>( fileRuns[ low ].file ) );
}

/*
================
idProgram::DefName
================
*/
const char *idProgram::DefName( const idVarDef *def ) const {
	if ( defs != NULL && def >= defs && def < defs + numDefs ) {
		const unsigned short index = static_cast<unsigned short>( def - defs );
		for ( int i = 0; i < numLookups; i++ ) {
			if ( lookups[ i ].def == index ) {
				return strings + lookups[ i ].name;
			}
		}
	}

	return "";
}

/*
================
idProgram::FindType
================
*/
idTypeDef *idProgram::FindType( const char *name ) {
	// as id's program, only the program's own types (no built-in one) are found by name
	for ( int i = ps2::game::script::image::kBuiltinTypes; i < numTypes; i++ ) {
		if ( !strcmp( typeTable[ i ]->Name(), name ) ) {
			return typeTable[ i ];
		}
	}

	return NULL;
}

/*
================
idProgram::ScopeParent

The namespace or object a scope def was defined in; the global namespace has none.
================
*/
idVarDef *idProgram::ScopeParent( const idVarDef *scope ) const {
	if ( scope == &def_namespace || scope < defs || scope >= defs + numDefs ) {
		return NULL;
	}

	const unsigned short index = static_cast<unsigned short>( scope - defs );
	int low = 0;
	int high = numScopes - 1;
	while ( low <= high ) {
		const int middle = ( low + high ) / 2;
		if ( scopes[ middle ].def == index ) {
			return ( scopes[ middle ].parent == ps2::game::script::image::kNone ) ? &def_namespace : &defs[ scopes[ middle ].parent ];
		}
		if ( scopes[ middle ].def < index ) {
			low = middle + 1;
		} else {
			high = middle - 1;
		}
	}

	return NULL;
}

/*
================
idProgram::ScopeDepth

idVarDef::DepthOfScope: how many scopes out from otherScope the def's scope lies, or 0.
================
*/
int idProgram::ScopeDepth( const idVarDef *defScope, const idVarDef *otherScope ) const {
	int depth = 1;
	for( const idVarDef *scope = otherScope; scope != NULL; scope = ScopeParent( scope ) ) {
		if ( scope == defScope ) {
			return depth;
		}
		depth++;
	}

	return 0;
}

/*
================
idProgram::GetDef

If type is NULL, it will match any type. The cooked lookups hold every def a namespace or object
scope can reach, sorted by name.
================
*/
idVarDef *idProgram::GetDef( const idTypeDef *type, const char *name, const idVarDef *scope ) const {
	// the first lookup with this name
	int low = 0;
	int high = numLookups;
	while ( low < high ) {
		const int middle = ( low + high ) / 2;
		if ( idStr::Cmp( strings + lookups[ middle ].name, name ) < 0 ) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}

	idVarDef *bestDef = NULL;
	int bestDepth = 0;
	for ( int i = low; i < numLookups && !idStr::Cmp( strings + lookups[ i ].name, name ); i++ ) {
		idVarDef *def = &defs[ lookups[ i ].def ];
		const idVarDef *defScope = ( lookups[ i ].scope == ps2::game::script::image::kNone ) ? &def_namespace : &defs[ lookups[ i ].scope ];
		int depth;

		if ( defScope->Type() == ev_namespace ) {
			depth = ScopeDepth( defScope, scope );
			if ( !depth ) {
				// not in the same namespace
				continue;
			}
		} else if ( defScope != scope ) {
			// in a different object
			continue;
		} else {
			depth = 1;
		}

		if ( !bestDef || ( depth < bestDepth ) ) {
			bestDepth = depth;
			bestDef = def;
		}
	}

	// see if the name is already in use for another type
	if ( bestDef && type && ( bestDef->TypeDef() != type ) ) {
		gameLocal.Error( "Type mismatch on redeclaration of %s", name );
	}

	return bestDef;
}

/*
================
idProgram::FindFunction

Searches for the specified function in the currently loaded script.  A full namespace should be
specified if not in the global namespace.

Returns 0 if function not found.
Returns >0 if function found.
================
*/
function_t *idProgram::FindFunction( const char *name ) const {
	int			start;
	int			pos;
	idVarDef	*namespaceDef;
	idVarDef	*def;

	assert( name );

	idStr fullname = name;
	start = 0;
	namespaceDef = &def_namespace;
	do {
		pos = fullname.Find( "::", true, start );
		if ( pos < 0 ) {
			break;
		}

		idStr namespaceName = fullname.Mid( start, pos - start );
		def = GetDef( NULL, namespaceName, namespaceDef );
		if ( !def ) {
			// couldn't find namespace
			return NULL;
		}
		namespaceDef = def;

		// skip past the ::
		start = pos + 2;
	} while( def->Type() == ev_namespace );

	idStr funcName = fullname.Right( fullname.Length() - start );
	def = GetDef( NULL, funcName, namespaceDef );
	if ( !def ) {
		// couldn't find function
		return NULL;
	}

	if ( ( def->Type() == ev_function ) && ( def->value.functionPtr->eventdef == NULL ) ) {
		return def->value.functionPtr;
	}

	// is not a function, or is an eventdef
	return NULL;
}

/*
================
idProgram::FindFunction

Searches for the specified object function in the currently loaded script.

Returns 0 if function not found.
Returns >0 if function found.
================
*/
function_t *idProgram::FindFunction( const char *name, const idTypeDef *type ) const {
	const idVarDef	*tdef;
	const idVarDef	*def;

	// look for the function
	def = NULL;
	for( tdef = type->def; tdef != &def_object; tdef = tdef->TypeDef()->SuperClass()->def ) {
		def = GetDef( NULL, name, tdef );
		if ( def ) {
			return def->value.functionPtr;
		}
	}

	return NULL;
}

/*
================
idProgram::SetEntity
================
*/
void idProgram::SetEntity( const char *name, idEntity *ent ) {
	idVarDef	*def;
	idStr		defName( "$" );

	defName += name;

	def = GetDef( &type_entity, defName, &def_namespace );
	if ( def != NULL && ( def->initialized != idVarDef::stackVariable ) ) {
		// 0 is reserved for NULL entity
		if ( !ent ) {
			*def->value.entityNumberPtr = 0;
		} else {
			*def->value.entityNumberPtr = ent->entityNumber + 1;
		}
	}
}

/*
================
idProgram::ReturnEntity
================
*/
void idProgram::ReturnEntity( idEntity *ent ) {
	if ( ent ) {
		*returnDef->value.entityNumberPtr = ent->entityNumber + 1;
	} else {
		*returnDef->value.entityNumberPtr = 0;
	}
}

#endif // defined( ID_SCRIPT_IMAGE )
