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
// [PS2_D3BFG]: Include paths match the on-disk case for case-sensitive build hosts.
#include "../../idlib/precompiled.h"
// [PS2_D3BFG]: Preserve nullable Shell hierarchy queries without compiler RTTI.
#include "ps2/type_query.h"
#include "../Game_local.h"

static const int NUM_CREDIT_LINES = 16;

void idMenuScreen_Shell_Credits::SetupCreditList() {

	class idRefreshCredits : public idSWFScriptFunction_RefCounted {
	public:
		idRefreshCredits( idMenuScreen_Shell_Credits * _screen ) :
			screen( _screen ) {
		}

		idSWFScriptVar Call( idSWFScriptObject * thisObject, const idSWFParmList & parms ) {

			if ( screen == NULL ) {
				return idSWFScriptVar();
			}

			screen->UpdateCredits();			
			return idSWFScriptVar();
		}
	private:
		idMenuScreen_Shell_Credits * screen;
	};	

	if ( GetSWFObject() ) {
		GetSWFObject()->SetGlobal( "updateCredits", new ( TAG_SWF ) idRefreshCredits( this ) );
	}


	// [PS2_D3BFG]: The credits are a data table instead of 635 Append calls (about 23 KB of
	// code); a type of -1 with an empty entry is the default creditInfo_t spacer.
	static const struct {
		int				type;
		const char *	entry;
	} creditEntries[] = {
		{ 3,	"DOOM 3 BFG EDITION"									},
		{ -1,	"" },
		{ 2,	"DEVELOPMENT TEAM"										},
		{ -1,	"" },
		{ 1,	"Executive Producer"									},
		{ 0,	"Eric Webb"												},
		{ -1,	"" },
		{ 1,	"Lead Programmer"										},
		{ 0,	"Brian Harris"											},
		{ -1,	"" },
		{ 1,	"Lead Designer"											},
		{ 0,	"Jerry Keehan"											},
		{ -1,	"" },
		{ 1,	"Programming"											},
		{ 0,	"Curtis Arink"											},
		{ 0,	"Robert Duffy"											},
		{ 0,	"Jeff Farrand"											},
		{ 0,	"Ryan Gerleve"											},
		{ 0,	"Billy Khan"											},
		{ 0,	"Gloria Kennickell"										},
		{ 0,	"Mike Maynard"											},
		{ 0,	"John Roberts"											},
		{ 0,	"Steven Serafin"										},
		{ 0,	"Jan Paul Van Waveren"									},
		{ -1,	"" },
		{ 1,	"Design"												},
		{ 0,	"Steve Rescoe"											},
		{ 0,	"Chris Voss"											},
		{ 0,	"David Vargo"											},
		{ -1,	"" },
		{ 1,	"Art"													},
		{ 0,	"Kevin Cloud"											},
		{ 0,	"Andy Chang"											},
		{ 0,	"Pat Duffy"												},
		{ -1,	"" },
		{ 1,	"Audio"													},
		{ 0,	"Christian Antkow"										},
		{ 0,	"Chris Hite"											},
		{ -1,	"" },
		{ 1,	"Additional Support"									},
		{ 0,	"Roger Berrones"										},
		{ 0,	"Brad Bramlett"											},
		{ 0,	"John Casey"											},
		{ 0,	"Jeremy Cook"											},
		{ 0,	"Chris Hays"											},
		{ 0,	"Matt Hooper"											},
		{ 0,	"Danny Keys"											},
		{ 0,	"Jason Kim"												},
		{ 0,	"Brian Kowalczyk"										},
		{ 0,	"Dustin Land"											},
		{ 0,	"Stephane Lebrun"										},
		{ 0,	"Matt Nelson"											},
		{ 0,	"John Pollard"											},
		{ 0,	"Alan Rogers"											},
		{ 0,	"Jah Raphael"											},
		{ 0,	"Jarrod Showers"										},
		{ 0,	"Shale Williams"										},
		{ -1,	"" },
		{ 1,	"Studio Director"										},
		{ 0,	"Tim Willits"											},
		{ -1,	"" },
		{ 1,	"Studio President"										},
		{ 0,	"Todd Hollenshead"										},
		{ -1,	"" },
		{ 1,	"Technical Director"									},
		{ 0,	"John Carmack"											},
		{ -1,	"" },
		{ 1,	"Human Resources"										},
		{ 0,	"Carrie Barcroft"										},
		{ -1,	"" },
		{ 1,	"id Mom"												},
		{ 0,	"Donna Jackson"											},
		{ -1,	"" },
		{ 1,	"id Software IT"										},
		{ 0,	"Duncan Welch"											},
		{ 0,	"Michael Cave"											},
		{ 0,	"Josh Shoemate"											},
		{ 0,	"Michael Musick"										},
		{ 0,	"Alex Brandt"											},
		{ -1,	"" },
		{ 3,	"DOOM 3 DEVELOPED BY"									},
		{ -1,	"" },
		{ 2,	"id Software"											},
		{ -1,	"" },
		{ 1,	"Technical Director "									},
		{ 0,	"John Carmack"											},
		{ -1,	"" },
		{ 1,	"Lead Programmer"										},
		{ 0,	"Robert A. Duffy"										},
		{ -1,	"" },
		{ 1,	"Lead Designer"											},
		{ 0,	"Tim Willits"											},
		{ -1,	"" },
		{ 1,	"Lead Artist"											},
		{ 0,	"Kenneth Scott"											},
		{ -1,	"" },
		{ 1,	"Animation"												},
		{ 0,	"James Houska"											},
		{ 0,	"Fredrik Nilsson"										},
		{ 0,	"Eric Webb"												},
		{ -1,	"" },
		{ 1,	"Art"													},
		{ 0,	"Adrian Carmack"										},
		{ 0,	"Andy Chang"											},
		{ 0,	"Kevin Cloud"											},
		{ 0,	"Seneca Menard"											},
		{ 0,	"Patrick Thomas"										},
		{ -1,	"" },
		{ 1,	"Design"												},
		{ 0,	"Mal Blackwell"											},
		{ 0,	"Matt Hooper"											},
		{ 0,	"Jerry Keehan"											},
		{ 0,	"Steve Rescoe"											},
		{ -1,	"" },
		{ 1,	"UI Artist"												},
		{ 0,	"Pat Duffy"												},
		{ -1,	"" },
		{ 1,	"Programming"											},
		{ 0,	"Timothee Besset"										},
		{ 0,	"Jim Dose"												},
		{ 0,	"Jan Paul van Waveren"									},
		{ 0,	"Jonathan Wright"										},
		{ 0,	"Brian Harris"											},
		{ -1,	"" },
		{ 1,	"Sound Design "											},
		{ 0,	"Christian Antkow"										},
		{ -1,	"" },
		{ 1,	"Dir. Business Development"								},
		{ 0,	"Marty Stratton"										},
		{ -1,	"" },
		{ 1,	"Development Assistant"									},
		{ 0,	"Eric Webb"												},
		{ -1,	"" },
		{ 2,	"3rd PARTY"												},
		{ -1,	"" },
		{ 1,	"DOOM 3: Ressurection of Evil Developed by"				},
		{ 0,	"Nerve Software"										},
		{ -1,	"" },
		{ 1,	"Additional Sound Design"								},
		{ 0,	"Ed Lima"												},
		{ 0,	"Danetracks, Inc."										},
		{ 0,	"Defacto Sound"											},
		{ -1,	"" },
		{ 1,	"Theme for DOOM 3 Produced by"							},
		{ 0,	"Chris Vrenna"											},
		{ -1,	"" },
		{ 1,	"Theme for DOOM 3 Composed by"							},
		{ 0,	"Clint Walsh"											},
		{ -1,	"" },
		{ 1,	"Additional Story and Dialog"							},
		{ 0,	"Matthew J. Costello"									},
		{ -1,	"" },
		{ 1,	"Casting and Voice Direction"							},
		{ 0,	"Margaret Tang - Womb Music"							},
		{ -1,	"" },
		{ 1,	"Voice Editorial and Post"								},
		{ 0,	"Rik Schaffer - Womb Music"								},
		{ -1,	"" },
		{ 1,	"Voice Recording"										},
		{ 0,	"Womb Music"											},
		{ -1,	"" },
		{ 1,	"Voice Performers"										},
		{ 0,	"Paul Eiding"											},
		{ 0,	"Jim Cummings"											},
		{ 0,	"Liam O'Brien"											},
		{ 0,	"Nicholas Guest"										},
		{ 0,	"Vanessa Marshall"										},
		{ 0,	"Neill Ross"											},
		{ 0,	"Phillip Clarke"										},
		{ 0,	"Andy Chanley"											},
		{ 0,	"Charles Dennis"										},
		{ 0,	"Grey Delisle"											},
		{ 0,	"Jennifer Hale"											},
		{ 0,	"Grant Albrecht"										},
		{ 0,	"Dee Baker"												},
		{ 0,	"Michael Bell"											},
		{ 0,	"Steve Blum"											},
		{ 0,	"S. Scott Bullock"										},
		{ 0,	"Cam Clarke"											},
		{ 0,	"Robin Atkin Downes"									},
		{ 0,	"Keith Ferguson"										},
		{ 0,	"Jay Gordon"											},
		{ 0,	"Michael Gough"											},
		{ 0,	"Bill Harper"											},
		{ 0,	"Nick Jameson"											},
		{ 0,	"David Kaye"											},
		{ 0,	"Phil La Marr"											},
		{ 0,	"Scott Menville"										},
		{ 0,	"Jim Meskimen"											},
		{ 0,	"Matt Morton"											},
		{ 0,	"Daran Norris"											},
		{ 0,	"Rob Paulsen"											},
		{ 0,	"Phil Proctor"											},
		{ 0,	"Rino Romoano"											},
		{ 0,	"Andre Sogliuzzo"										},
		{ 0,	"Jim Ward"												},
		{ 0,	"Wally Wingert"											},
		{ 0,	"Edward Yin"											},
		{ 0,	"Keone Young"											},
		{ 0,	"Ryun Yu"												},
		{ -1,	"" },
		{ 1,	"Additional User Interface"								},
		{ 0,	"Double-Action Design"									},
		{ -1,	"" },
		{ 1,	"Motion Capture Services"								},
		{ 0,	"Janimation"											},
		{ -1,	"" },
		{ 1,	"UAC Promotional Videos"								},
		{ 0,	"Six Foot Studios"										},
		{ -1,	"" },
		{ 1,	"Additional Multiplayer Design"							},
		{ 0,	"Splash Damage, Ltd."									},
		{ -1,	"" },
		{ 1,	"Additional Programming"								},
		{ 0,	"Graeme Devine"											},
		{ -1,	"" },
		{ 1,	"Localization Services"									},
		{ 0,	"Synthesis"												},
		{ -1,	"" },
		{ -1,	"" },
		{ 3,	"BETHESDA SOFTWORKS"									},
		{ -1,	"" },
		{ 1,	"Senior Producer"										},
		{ 0,	"Laffy Taylor"											},
		{ -1,	"" },
		{ 1,	"Sr. Producer, Submissions"								},
		{ 0,	"Timothy Beggs"											},
		{ -1,	"" },
		{ 1,	"VP, Product Development"								},
		{ 0,	"Todd Vaughn"											},
		{ -1,	"" },
		{ 1,	"External Technical Director"							},
		{ 0,	"Jonathan Williams"										},
		{ -1,	"" },
		{ 1,	"President"												},
		{ 0,	"Vlatko Andonov"										},
		{ -1,	"" },
		{ 1,	"VP, Public Relations and Marketing"					},
		{ 0,	"Pete Hines"											},
		{ -1,	"" },
		{ 1,	"Director of Global Marketing"							},
		{ 0,	"Steve Perkins"											},
		{ -1,	"" },
		{ 1,	"Director of Global PR"									},
		{ 0,	"Tracey Thompson"										},
		{ -1,	"" },
		{ 1,	"Senior Brand Manager"									},
		{ 0,	"Rakhi Gupta"											},
		{ -1,	"" },
		{ 1,	"Associate Brand Manager"								},
		{ 0,	"David Clayman"											},
		{ -1,	"" },
		{ 1,	"Event and Trade Show Director"							},
		{ 0,	"Henry Mobley"											},
		{ -1,	"" },
		{ 1,	"Senior Community Manager"								},
		{ 0,	"Matthew Grandstaff"									},
		{ -1,	"" },
		{ 1,	"Community Manager"										},
		{ 0,	"Nick Breckon"											},
		{ -1,	"" },
		{ 1,	"Senior PR Coordinator"									},
		{ 0,	"Angela Ramsey-Chapman"									},
		{ -1,	"" },
		{ 1,	"Marketing Assistant"									},
		{ 0,	"Jenny McWhorter"										},
		{ -1,	"" },
		{ 1,	"Video Production"										},
		{ 0,	"Matt Killmon"											},
		{ 0,	"Sal Goldenburg"										},
		{ -1,	"" },
		{ 1,	"Graphic Design"										},
		{ 0,	"Michael Wagner"										},
		{ 0,	"Lindsay Westcott"										},
		{ -1,	"" },
		{ 1,	"VP, Sales"												},
		{ 0,	"Ron Seger"												},
		{ -1,	"" },
		{ 1,	"Operations Director"									},
		{ 0,	"Todd Curtis"											},
		{ -1,	"" },
		{ 1,	"Sales and Operations Manager"							},
		{ 0,	"Jill Bralove"											},
		{ -1,	"" },
		{ 1,	"Sales Account Manager"									},
		{ 0,	"Michelle Ferrara"										},
		{ -1,	"" },
		{ 1,	"Regional Sales Manager"								},
		{ 0,	"Michael Donnellan"										},
		{ -1,	"" },
		{ 1,	"Channel Marketing Manager"								},
		{ 0,	"Michelle Burgess"										},
		{ -1,	"" },
		{ 1,	"Sales Assistants"										},
		{ 0,	"Sara Simpson"											},
		{ 0,	"Jason Snead"											},
		{ 0,	"Jessica Williams"										},
		{ -1,	"" },
		{ 1,	"Operations Assistant"									},
		{ 0,	"Scott Mills"											},
		{ -1,	"" },
		{ 1,	"Director of Quality Assurance"							},
		{ 0,	"Darren Manes"											},
		{ -1,	"" },
		{ 1,	"Quality Assurance Manager"								},
		{ 0,	"Rob Gray"												},
		{ -1,	"" },
		{ 1,	"Quality Assurance Leads"								},
		{ 0,	"Terry Dunn"											},
		{ 0,	"Matt Weil"												},
		{ -1,	"" },
		{ 1,	"Quality Assurance"										},
		{ 0,	"Dan Silva"												},
		{ 0,	"Alan Webb"												},
		{ 0,	"Cory Andrews"											},
		{ 0,	"George Churchill"										},
		{ 0,	"Jonathan DeVriendt"									},
		{ 0,	"Spencer Gottlieb"										},
		{ 0,	"Gary Powell"											},
		{ 0,	"Samuel Papke"											},
		{ 0,	"Amanda Sheehan"										},
		{ 0,	"Philip Spangrud"										},
		{ 0,	"Larry Waldman"											},
		{ 0,	"Donald Anderson"										},
		{ 0,	"James Audet"											},
		{ 0,	"Hunter Calvert"										},
		{ 0,	"Max Cameron"											},
		{ 0,	"Donald Harris"											},
		{ 0,	"Peter Garcia"											},
		{ 0,	"Joseph Lopatta"										},
		{ 0,	"Scott LoPresti"										},
		{ 0,	"Collin Mackett"										},
		{ 0,	"Gerard Nagy"											},
		{ 0,	"William Pegus"											},
		{ 0,	"Angela Rupinen"										},
		{ 0,	"Gabriel Scaringello"									},
		{ 0,	"Drew Slotkin"											},
		{ 0,	"Kyle Wallace"											},
		{ 0,	"Patrick Walsh"											},
		{ 0,	"John Welkner"											},
		{ 0,	"Jason Wilkin"											},
		{ 0,	"John Benoit"											},
		{ 0,	"Jacob Clayman"											},
		{ 0,	"Colin McInerney"										},
		{ -1,	"" },
		{ 1,	"Additional QA"											},
		{ 0,	"James Costantino"										},
		{ 0,	"Chris Krietz"											},
		{ 0,	"Joseph Mueller"										},
		{ 0,	"Andrew Scharf"											},
		{ 0,	"Jen Tonon"												},
		{ 0,	"Justin McSweeney"										},
		{ 0,	"Sean Palomino"											},
		{ 0,	"Erica Stead"											},
		{ 0,	"Wil Cookman"											},
		{ 0,	"Garrett Hohl"											},
		{ 0,	"Tim Hartgrave"											},
		{ 0,	"Heath Hollenshead"										},
		{ 0,	"Brandon Korbel"										},
		{ 0,	"Daniel Korecki"										},
		{ 0,	"Max Morrison"											},
		{ 0,	"Doug Ransley"											},
		{ 0,	"Mauricio Rivera"										},
		{ 0,	"Aaron Walsh"											},
		{ 0,	"Daniel Wathen"											},
		{ -1,	"" },
		{ 1,	"Special Thanks"										},
		{ 0,	"Jason Bergman"											},
		{ 0,	"Darren Chukitus"										},
		{ 0,	"Matt Dickenson"										},
		{ 0,	"Will Noble"											},
		{ -1,	"" },
		{ -1,	"" },
		{ 3,	"ZENIMAX MEDIA"											},
		{ -1,	"" },
		{ 1,	"Chairman & CEO"										},
		{ 0,	"Robert Altman"											},
		{ -1,	"" },
		{ 1,	"President"												},
		{ 0,	"Ernie Del"												},
		{ -1,	"" },
		{ 1,	"EVP & COO"												},
		{ 0,	"Jamie Leder"											},
		{ -1,	"" },
		{ 1,	"EVP & CFO"												},
		{ 0,	"Cindy Tallent"											},
		{ -1,	"" },
		{ 1,	"EVP Legal & Secretary"									},
		{ 0,	"Grif Lesher"											},
		{ -1,	"" },
		{ 1,	"SVP Finance & Controller"								},
		{ 0,	"Denise Kidd"											},
		{ -1,	"" },
		{ 1,	"Legal Lead"											},
		{ 0,	"Joshua Gillespie"										},
		{ -1,	"" },
		{ 1,	"Legal"													},
		{ 0,	"Diana Bender"											},
		{ 0,	"Adam Carter"											},
		{ 0,	"Candice Garner-Groves"									},
		{ 0,	"Marcia Mitnick"										},
		{ 0,	"Amy Yeung"												},
		{ -1,	"" },
		{ 1,	"VP, Information Technology"							},
		{ 0,	"Steve Bloom"											},
		{ -1,	"" },
		{ 1,	"Information Technology"								},
		{ 0,	"Rob Havlovick"											},
		{ 0,	"Nicholas Lea"											},
		{ 0,	"Drew McCartney"										},
		{ 0,	"Josh Mosby"											},
		{ 0,	"Joseph Owens"											},
		{ 0,	"Paul Tuttle"											},
		{ 0,	"Keelian Wardle"										},
		{ -1,	"" },
		{ 1,	"Director of Global HR"									},
		{ 0,	"Tammy Boyd-Shumway"									},
		{ -1,	"" },
		{ 1,	"Human Resources"										},
		{ 0,	"Michelle Cool"											},
		{ 0,	"Andrea Glinski"										},
		{ 0,	"Katrina Lang"											},
		{ 0,	"Valery Ridore"											},
		{ -1,	"" },
		{ 1,	"Administration"										},
		{ 0,	"Melissa Ayala"											},
		{ 0,	"Brittany Bezawada-Joseph"								},
		{ 0,	"Rosanna Campanile  "									},
		{ 0,	"Katherine Edwards"										},
		{ 0,	"Douglas Fredrick    "									},
		{ 0,	"Jon Freund"											},
		{ 0,	"Ken Garcia "											},
		{ 0,	"Gerard Garnica"										},
		{ 0,	"Betty Kouatelay"										},
		{ 0,	"Ho Joong Lee"											},
		{ 0,	"Barb Manning"											},
		{ 0,	"Stephane Marquis"										},
		{ 0,	"Michael Masciola"										},
		{ 0,	"Tanuja Mistry"											},
		{ 0,	"Rissa Monzano"											},
		{ 0,	"Patrick Nolan "										},
		{ 0,	"Patti Pulupa"											},
		{ 0,	"Dave Rasmussen "										},
		{ 0,	"Heather Spurrier"										},
		{ 0,	"Claudia Umana"											},
		{ 0,	"Melissa Washabaugh"									},
		{ 0,	"Eric Weis"												},
		{ -1,	"" },
		{ 1,	"Administrative Assistants"								},
		{ 0,	"Bernice Guice"											},
		{ 0,	"Paula Kasey"											},
		{ 0,	"Shana Reed"											},
		{ -1,	"" },
		{ 1,	"Executive Chef"										},
		{ 0,	"Kenny McDonald"										},
		{ -1,	"" },
		{ 3,	"ZENIMAX EUROPE"										},
		{ -1,	"" },
		{ 1,	"European Managing Director"							},
		{ 0,	"Sean Brennan"											},
		{ -1,	"" },
		{ 1,	"European Marketing & PR Director"						},
		{ 0,	"Sarah Seaby"											},
		{ -1,	"" },
		{ 1,	"European Sales Director"								},
		{ 0,	"Paul Oughton"											},
		{ -1,	"" },
		{ 1,	"Sales Director"										},
		{ 0,	"Greg Baverstock"										},
		{ -1,	"" },
		{ 1,	"Finance Director"										},
		{ 0,	"Robert Ford"											},
		{ -1,	"" },
		{ 1,	"European Brand Marketing Manager"						},
		{ 0,	"Thach Quach"											},
		{ -1,	"" },
		{ 1,	"European Brand Marketing Manager"						},
		{ 0,	"Alex Price"											},
		{ -1,	"" },
		{ 1,	"European PR Manager"									},
		{ 0,	"Alistair Hatch"										},
		{ -1,	"" },
		{ 1,	"European Assistant PR Manager"							},
		{ 0,	"Nicholas Heller"										},
		{ -1,	"" },
		{ 1,	"International Marketing Manager"						},
		{ 0,	"Rosemarie Dalton"										},
		{ -1,	"" },
		{ 1,	"Manager Trade Shows/Events"							},
		{ 0,	"Gareth Swann"											},
		{ -1,	"" },
		{ 1,	"UK Marketing Manager"									},
		{ 0,	"Gregory Weller"										},
		{ -1,	"" },
		{ 1,	"UK Sales Manager"										},
		{ 0,	"Gethyn Deakins"										},
		{ -1,	"" },
		{ 1,	"Creative Services Artist"								},
		{ 0,	"Morgan Gibbons"										},
		{ -1,	"" },
		{ 1,	"Legal Counsel"											},
		{ 0,	"Adam Carter"											},
		{ -1,	"" },
		{ 1,	"Localisation Director"									},
		{ 0,	"Harald Simon"											},
		{ -1,	"" },
		{ 1,	"Localisation Project Manager"							},
		{ 0,	"Ruth Granados Garcia"									},
		{ -1,	"" },
		{ 1,	"Sales Administrator"									},
		{ 0,	"Heather Clarke"										},
		{ -1,	"" },
		{ 1,	"Operations Manager"									},
		{ 0,	"Isabelle Midrouillet"									},
		{ -1,	"" },
		{ 1,	"Operations Coordinator"								},
		{ 0,	"David Gordon"											},
		{ -1,	"" },
		{ 1,	"Financial Controller"									},
		{ 0,	"Paul New"												},
		{ -1,	"" },
		{ 1,	"Accounts Assistant"									},
		{ 0,	"Charlotte Ovens"										},
		{ -1,	"" },
		{ 1,	"Contracts Administrator/Paralegal"						},
		{ 0,	"Katie Brooks"											},
		{ -1,	"" },
		{ 1,	"IT Manager"											},
		{ 0,	"Joseph Owens"											},
		{ -1,	"" },
		{ 1,	"Office Manager"										},
		{ 0,	"Angela Clement"										},
		{ -1,	"" },
		{ 3,	"ZENIMAX FRANCE"										},
		{ -1,	"" },
		{ 1,	"General Manager"										},
		{ 0,	"Julie Chalmette"										},
		{ -1,	"" },
		{ 1,	"Sales Manager"											},
		{ 0,	"Yvan Rault"											},
		{ -1,	"" },
		{ 1,	"Key Account Manager"									},
		{ 0,	"Gaelle Gombert"										},
		{ -1,	"" },
		{ 1,	"Trade Marketing Manager"								},
		{ 0,	"Laurent Chatain"										},
		{ -1,	"" },
		{ 1,	"Marketing Manager"										},
		{ 0,	"Geraldine Mazot"										},
		{ -1,	"" },
		{ 1,	"PR Manager"											},
		{ 0,	"Jerome Firon"											},
		{ -1,	"" },
		{ 1,	"Finance Manager"										},
		{ 0,	"Cecile De Freitas"										},
		{ -1,	"" },
		{ 1,	"Finance Assistant"										},
		{ 0,	"Adeline Nonis"											},
		{ -1,	"" },
		{ 3,	"ZENIMAX GERMANY"										},
		{ -1,	"" },
		{ 1,	"Managing Director"										},
		{ 0,	"Frank Matzke"											},
		{ -1,	"" },
		{ 1,	"Sales Director"										},
		{ 0,	"Thomas Huber"											},
		{ -1,	"" },
		{ 1,	"Head of Marketing & PR"								},
		{ 0,	"Marcel Jung"											},
		{ -1,	"" },
		{ 1,	"Product Marketing Manager"								},
		{ 0,	"Stefan Dettmering"										},
		{ -1,	"" },
		{ 1,	"PR Manager"											},
		{ 0,	"Peter Langhofer"										},
		{ -1,	"" },
		{ 1,	"Trade Marketing Manager"								},
		{ 0,	"Andrea Reuth"											},
		{ -1,	"" },
		{ 1,	"Key Account Manager"									},
		{ 0,	"Juergen Pahl"											},
		{ -1,	"" },
		{ 1,	"Finance Manager"										},
		{ 0,	"Joern Hoehling"										},
		{ -1,	"" },
		{ 1,	"Sales and Office Administrator"						},
		{ 0,	"Christiane Jauss"										},
		{ -1,	"" },
		{ 3,	"ZENIMAX BENELUX"										},
		{ -1,	"" },
		{ 1,	"General Manager"										},
		{ 0,	"Menno Eijck"											},
		{ -1,	"" },
		{ 1,	"Sales Manager"											},
		{ 0,	"Stefan Koppers"										},
		{ -1,	"" },
		{ 1,	"Marketing Manager"										},
		{ 0,	"Jurgen Stirnweis"										},
		{ -1,	"" },
		{ 1,	"PR Manager"											},
		{ 0,	"Maikel van Dijk"										},
		{ -1,	"" },
		{ 1,	"Sales and Marketing Administrator"						},
		{ 0,	"Pam van Griethuysen"									},
		{ -1,	"" },
		{ -1,	"" },
		{ 3,	"ZENIMAX ASIA"											},
		{ -1,	"" },
		{ 1,	"General Manager"										},
		{ 0,	"Tetsu Takahashi"										},
		{ -1,	"" },
		{ 1,	"Localization Producer "								},
		{ 0,	"Kei Iwamoto"											},
		{ -1,	"" },
		{ 1,	"Associate Producer "									},
		{ 0,	"Takayuki Tanaka"										},
		{ -1,	"" },
		{ 1,	"Product Manager "										},
		{ 0,	"Seigen Ko"												},
		{ -1,	"" },
		{ 1,	"Localization Programmer"								},
		{ 0,	"Masayuki Nagahashi"									},
		{ -1,	"" },
		{ 1,	"Marketing and PR Manager "								},
		{ 0,	"Eiichi Yaji"											},
		{ -1,	"" },
		{ 1,	"Sales Manager"											},
		{ 0,	"Hiroaki Yanagiguchi"									},
		{ -1,	"" },
		{ 1,	"Office Manager"										},
		{ 0,	"Myongsuk Rim"											},
		{ -1,	"" },
		{ 1,	"Web Director"											},
		{ 0,	"Tanaka Keisuke"										},
		{ 0,	"Kosuke Fujita"											},
	};

	creditList.Clear();
	for ( int i = 0; i < sizeof( creditEntries ) / sizeof( creditEntries[0] ); i++ ) {
		creditList.Append( creditInfo_t( creditEntries[i].type, creditEntries[i].entry ) );
	}
};

/*
========================
idMenuScreen_Shell_Credits::Initialize
========================
*/
void idMenuScreen_Shell_Credits::Initialize( idMenuHandler * data ) {
	idMenuScreen::Initialize( data );

	if ( data != NULL ) {
		menuGUI = data->GetGUI();
	}

	SetSpritePath( "menuCredits" );

	btnBack = new (TAG_SWF) idMenuWidget_Button();
	btnBack->Initialize( data );	
	btnBack->SetLabel( "#str_02305" );
	btnBack->SetSpritePath( GetSpritePath(), "info", "btnBack" );
	btnBack->AddEventAction( WIDGET_EVENT_PRESS ).Set( WIDGET_ACTION_GO_BACK );
	AddChild( btnBack );

	SetupCreditList();
}

/*
========================
idMenuScreen_Shell_Credits::Update
========================
*/
void idMenuScreen_Shell_Credits::Update() {

	if ( menuData != NULL ) {
		idMenuWidget_CommandBar * cmdBar = menuData->GetCmdBar();
		if ( cmdBar != NULL ) {
			cmdBar->ClearAllButtons();

			idMenuHandler_Shell * shell = ps2::DynCast< idMenuHandler_Shell * >( menuData );
			bool complete = false;
			if ( shell != NULL ) {
				complete = shell->GetGameComplete();
			}

			idMenuWidget_CommandBar::buttonInfo_t * buttonInfo;
			if ( !complete ) {
				buttonInfo = cmdBar->GetButton( idMenuWidget_CommandBar::BUTTON_JOY2 );
				if ( menuData->GetPlatform() != 2 ) {
					buttonInfo->label = "#str_00395";
				}
				buttonInfo->action.Set( WIDGET_ACTION_GO_BACK );
			} else {
				buttonInfo = cmdBar->GetButton( idMenuWidget_CommandBar::BUTTON_JOY1 );
				if ( menuData->GetPlatform() != 2 ) {
					buttonInfo->label = "#str_swf_continue";
				}
				buttonInfo->action.Set( WIDGET_ACTION_GO_BACK );
			}
		}		
	}

	idSWFScriptObject & root = GetSWFObject()->GetRootObject();
	if ( BindSprite( root ) ) {
		idSWFTextInstance * heading = GetSprite()->GetScriptObject()->GetNestedText( "info", "txtHeading" );
		if ( heading != NULL ) {
			heading->SetText( "#str_02218" );
			heading->SetStrokeInfo( true, 0.75f, 1.75f );
		}
	}

	if ( btnBack != NULL ) {
		btnBack->BindSprite( root );
	}

	idMenuScreen::Update();
}

/*
========================
idMenuScreen_Shell_Credits::ShowScreen
========================
*/
void idMenuScreen_Shell_Credits::ShowScreen( const mainMenuTransition_t transitionType ) {

	if ( menuData != NULL ) {
		idMenuHandler_Shell * shell = ps2::DynCast< idMenuHandler_Shell * >( menuData );
		bool complete = false;
		if ( shell != NULL ) {
			complete = shell->GetGameComplete();
		}

		if ( complete ) {
			menuData->PlaySound( GUI_SOUND_MUSIC );
		}
	}

	idMenuScreen::ShowScreen( transitionType );
	creditIndex = 0;
	UpdateCredits();
}

/*
========================
idMenuScreen_Shell_Credits::HideScreen
========================
*/
void idMenuScreen_Shell_Credits::HideScreen( const mainMenuTransition_t transitionType ) {
	idMenuScreen::HideScreen( transitionType );
}

/*
========================
idMenuScreen_Shell_Credits::HandleAction
========================
*/
bool idMenuScreen_Shell_Credits::HandleAction( idWidgetAction & action, const idWidgetEvent & event, idMenuWidget * widget, bool forceHandled ) {

	if ( menuData == NULL ) {
		return true;
	}

	if ( menuData->ActiveScreen() != SHELL_AREA_CREDITS ) {
		return false;
	}

	widgetAction_t actionType = action.GetType();
	switch ( actionType ) {
		case WIDGET_ACTION_GO_BACK: {

			idMenuHandler_Shell * shell = ps2::DynCast< idMenuHandler_Shell * >( menuData );
			bool complete = false;
			if ( shell != NULL ) {
				complete = shell->GetGameComplete();
			}

			if ( complete ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
			} else {
				menuData->SetNextScreen( SHELL_AREA_ROOT, MENU_TRANSITION_SIMPLE );
			}
			
			return true;
		}
	}

	return idMenuWidget::HandleAction( action, event, widget, forceHandled );
}

/*
========================
idMenuScreen_Shell_Credits::UpdateCredits
========================
*/
void idMenuScreen_Shell_Credits::UpdateCredits() {

	if ( menuData == NULL || GetSWFObject() == NULL ) {
		return;
	}

	if ( menuData->ActiveScreen() != SHELL_AREA_CREDITS && menuData->NextScreen() != SHELL_AREA_CREDITS  ) {
		return;
	}

	if ( creditIndex >= creditList.Num() + NUM_CREDIT_LINES ) {
		idMenuHandler_Shell * shell = ps2::DynCast< idMenuHandler_Shell * >( menuData );
			bool complete = false;
			if ( shell != NULL ) {
				complete = shell->GetGameComplete();
			}

			if ( complete ) {
				cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );
			} else {
				menuData->SetNextScreen( SHELL_AREA_ROOT, MENU_TRANSITION_SIMPLE );
			}
		return;
	}

	idSWFScriptObject * options = GetSWFObject()->GetRootObject().GetNestedObj( "menuCredits", "info", "options" );
	if ( options != NULL ) {
		for ( int i = 15; i >= 0; --i ) {			
			int curIndex = creditIndex - i;
			idSWFTextInstance * heading = options->GetNestedText( va( "item%d", 15 - i ), "heading" );
			idSWFTextInstance * subHeading = options->GetNestedText( va( "item%d", 15 - i ), "subHeading" );
			idSWFTextInstance * title = options->GetNestedText( va( "item%d", 15 - i ), "title" );
			idSWFTextInstance * txtEntry = options->GetNestedText( va( "item%d", 15 - i ), "entry" );

			if ( curIndex >= 0 && curIndex < creditList.Num() ) {

				int type = creditList[ curIndex ].type;
				idStr entry = creditList[ curIndex ].entry;

				if ( heading ) {
					heading->SetText( type == 3 ? entry : "" );
					heading->SetStrokeInfo( true );
				}

				if ( subHeading ) {
					subHeading->SetText( type == 2 ? entry : "" );
					subHeading->SetStrokeInfo( true );
				}
				
				if ( title ) {
					title->SetText( type == 1 ? entry : "" );
					title->SetStrokeInfo( true );
				}

				if ( txtEntry ) {
					txtEntry->SetText( type == 0 ? entry : "" );
					txtEntry->SetStrokeInfo( true );
				}

			} else {
				
				if ( heading ) {
					heading->SetText( "" );
				}

				if ( subHeading ) {
					subHeading->SetText( "" );
				}

				if ( txtEntry ) {
					txtEntry->SetText( "" );
				}

				if ( title ) {
					title->SetText( "" );
				}

			}
		}
		if ( options->GetSprite() ) {
			options->GetSprite()->PlayFrame( "roll" );
		}
	}

	creditIndex++;
}
