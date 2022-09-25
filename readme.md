Openglad
========

Openglad is a port of the open-sourced dos game known as Gladiator
(http://fsgames.com/glad/). It is a top-view gauntlet style RPG that
features fast paced multiplayer action, several different classes, and
a scenario editor.

Install
======

Read the INSTALL file for install instructions.

Playing
=======

Read the in-game help for help on how to play (press f1 in a level or read glad.hlp).

Openscen
========
Read scen.txt for information about openscen (howto, etc..)

Openglad
========

Note: as of July 1st, 2002, Gladiator is open sourced under the GPL.
The Forgotten Sages Development Team has graciously open sourced it,
and the developers of the Snowstorm team are sincerely thankful for
all the work that was saved.

Thank you, FSGames.

The readme file for the 3.8 release of Gladiator, written by FSGames, follows below.

Gladiator version 3.8 Readme.txt file.

NOTE: While we try to keep this README file up to date, some
      sections relating to the shareware version may be listed
      in the registered file, and vice versa.  Please bear with
      us and we'll get you a good product! :)
      -- Forgotten Sages Development Team

[0] Installation information

  For this release of Gladiator, it is important that you run the
  'setup' program after unzipping the files to your directory.
  This will ensure that the settings are correct for your system.
  If you wish to change the settings later, you can re-run setup,
  or manually edit the 'glad.cfg' text file.

  Gladiator features Sound Blaster compatible sound.
  Note that for sound to work, the "BLASTER" environment variable
  needs to be set in your autoexec.bat.  It should like:
        BLASTER=A220 I5 D1 T2
  or something similar.  Gladiator may, unfortunately, freeze up
  if your card is not 100% sound blaster compatible.  If it does
  this you have 2 options.
    1) You can try downloading updated sound card
       drivers from your sound card manufacturer.
    2) You can re-disable the sound, and look for
       an updated Gladiator version in case we
       have been able to resolve this problem.


[1] Keyboard Controls

  [1.a] Description
  Gladiator is designed to allow up to four players to use the
  keyboard at once (although we admit it would be rather
  cramped) and to use joysticks/gamepads.  Each player has a 
  number of 'action' keys, such as movement or attacking, and 
  also a set of preferences, which are reached from the 
  'options menu,' and control things such as game speed or 
  viewport size.  Important to remember are the 'ESC' key, which 
  will abort your current mission and return you to the 'picker' 
  screen, and the 'F1' key, which will bring up a brief help 
  display during the game.
  
  See glad.hlp for detailed controls.


[2] Overview
    Gladiator is a gauntlet style, real-time arcade game where you
  (the red team) must set out to destroy your enemies (and pick up
  treasure!) in a variety of scenarios.  You get to sharpen the 
  skills of your team as you progress through the levels, and each
  character class (Fighter, Mage, etc.) has a number of special
  abilities which come into play as your character gains levels.

[3] How to play -- a brief tutorial

A) The Picker
  When you first start a game of Gladiator, you actually start in
  the 'picker' (short for "Team Picker") instead of the playing
  field itself.  The picker's main menu gives you several choices:
  Begin a new game, continue with your current or saved game, or
  exit.  There is also a set of four buttons to control how many
  human players will be in the game (up to four), and a toggle to
  control the difficulty settings.  For now, select Begin New Game.

  The next screen of the picker allows you to save or load games (useful 
  later on), train your current team, view your team, or, finally, hire
  new members for your squad.  Since you don't have any team members yet,
  the first thing to do is "Hire Troops".

  Choosing the "Hire Troops" menu takes you to a screen which will display
  the name/type of a new recruit, his statistics, and the cost to
  purchase him, along with your current cash, which for a new game
  begins at 5000.  From this point you may buy this character, raise
  his stats (and cost) in various amounts, or, using the "NEXT" and
  "PREV" buttons near the top, see what other characters are availible.

  Generally, new abilities come every three levels (at levels 1, 4,
  7, and 10).  Sometimes you'll also have 'alternate' abilities granted
  at a given level; these often have other restrictions of use.  For
  example, the mage gets an alternate special -- teleport marker -- at
  level 1, but he must have at least 75 Intelligence before it becomes
  usable.

  A brief sample of basic troop types follows; for more detailed   
  information, including special abilities, consult the classes.txt file.
    * Soldier:  Your basic grunt, can absorb and deal damage and moves
                moderately fast.  A good all-around fighter.
    *     Elf:  Elves are small and weaker, but are harder to hit.
                Elves throw stones, sometimes more than one.
    *  Archer:  Archers are fleet of foot, and their arrows have a
                long range.  Although they're not as strong or healthy
                as fighters, they can also be a good squad backbone.
    *    Mage:  Mages are slow, nearly defenseless, and horrible at
                hand-to-hand combat, but their magical energy balls
                pack a big punch.  All mages, regardless of level,
                posess the ability to teleport out of (or into!) danger,
                providing they've saved enough magic.
    *  Cleric:  Clerics, also, are slow, but have a stronger hand-
                to-hand than mages.  Clerics have the unique ability to
                generate fields which their teammates can pass through, but
                which foes must hack down to pass.  Clerics also posess
                the ability to heal teammates who are standing next to
                them.
    *   Thief:  Theives are fast and stronger of constitution than the
                magic-wielders, though not so potent as the soldier.
                A thief, in addition to throwing daggers, can plant
                a bomb which can take out even the strongest opponent.
                Beware, however -- unlike an archer's arrows, bombs
                do not discriminate in who they destroy!  Skilled
                thieves can even hide themselves from their enemies'
                sight.
    *   Druid:  Druids are the magicians of nature, and have special
                powers over natural events.  Druids can throw lightning
                bolts at their foes, and can grow trees to impede the
                progress of their enemies.  At more powerful levels,
                the druids can even summon faeries to their aid.

    In general, each character has at least three 'special abilities,' and
    some have more, although it often requires gaining several levels
    before these other special abilities become availible.

    You will also come up against other foes -- undead such as skeletons
    and ghosts, magical beings such as elementals and faeries, and others.
    These types are availble for purchase only in the registered version
    of Gladiator, although some may join your team on their own at
    certain times.

    The statistics from which you can choose to modify here are:

    *     Strength: Strength affects your melee (hand-to-hand) combat,
                    as well as things like your weapon range.

    * Constitution: Constitution increases your hitpoints and rate of 
                    healing, and also affects other combat-related
                    skills.

    *    Dexterity: Dexterity helps improve your ability to dodge, 
                    your weapon's accuracy, your speed, and other
                    related skills.

    * Intelligence: Intelligence controls the amount of 'magic' power
                    you have, how fast this power regenerates, and
                    the success of your special abilities.

    *        Armor: Armor is like shielding; the more you have, the
                    less damage you will take from a given blow.

    *        Level: Your character's level is determined by his
                    experience (currently 0), visible on the right
                    of the screen.  Level affects all stats, and
                    affects what special abilities your character can
                    perform.  While it may look very expensive to buy
                    a level for your character, it can be useful when
                    you're "almost there" and want the increase, as the
                    cost decreases the closer you are to the next level.


  So, from the "Hire" menu you may purchase whatever type(s) of men you
  choose.  You can edit their stats before you buy them, or, if you
  prefer, switch to the "Train" menu instead.  The train menu appears
  similar to the purchasing menu, but the NEXT and PREV buttons will
  take you through your already purchased men.  When you use the "LESS"
  and "MORE" buttons for changing their stats, notice that you do not
  actually pay (and thus make your changes permanent) until you click
  the "ACCEPT" button; this will purchase the changes, provided you have
  the funds, and record your character's stats.

  Clicking the LEFT mouse button on "+" or "-" button will adjust the
  value by one point; clicking the RIGHT mouse button will change by
  five.

  After you have chosen to buy a new character (click the BUY button),
  you get a chance to name your new team member.  Hitting Enter will
  accept the default name, which will be something like SOLDIER1.

  When picking a team, only about the first 25, at most, will appear in
  a scenario; the others are not lost, but will show up on the next level.
  Thus, it is generally best to limit your team to 10 to 20 characters,
  which is normally quite sufficient.

  After you have purchased and edited your team, or any time during
  this phase, you can choose to "VIEW TEAM." This will display a
  screen listing the number of each type of character you have, and
  what "scenario" number you will be attempting next (currently 1).
  This screen also contains a "GO" button:  this will start that
  scenario. There is also a button titled "Set Level."  Clicking
  this button enables you to jump to any level in the game, including
  any levels you may have created with the scenario editor.

  Before hitting go, this time, press (or click) "ESC" several times
  until you get to a point where you can save the game.  Don't worry if
  you go out to the main menu; simply press "continue game" from there;
  all your team members will still be there.  When saving a game, you
  have a choice of 10 slots.  Your current game is always saved by the
  computer as game number 0, and is loaded each time you enter the
  picker.  Each saved game slot allows you a descriptive name to help
  keep games in order; just hit Enter after typing the name to save
  your game.

  After saving your game, you're ready to go!  Find the "GO" button
  (such as from the "VIEW TEAM" menu), click it, and you're off!

B) The Scenarios

  You've just hit the "GO" button from the picker.  After a brief
  fade-out,  you'll see the main screen, which by default will fill
  the entire screen.  Each corner of the screen (top left, etc.)
  contains some important information about the state of the game,
  but more on that in a moment.

  When the level first begins, another button with introduction text
  should appear in the center of the screen.  This text will often
  tell you about the current level, and may list any special goals or
  hints.  You can use the arrow keys or the page up/down keys to
  scroll through the text.  When you are done reading it, hit ESC to
  continue.

  The viewscreen lists several important items.  At the top left should 
  be listed the name or designation of your current character, such
  as "COMMANDER."  Below that are numbers and bar graphs indicating
  your current hitpoints and spellpoints.  Be careful -- when your
  hitpoints go to zero, you're dead!

  At the bottom left will appear "SC: 123" and "XP: 456"  This is your 
  score for the current level, which will start at zero and go up as you
  wreak havok, and your character's experience, which will depend on 
  how much damage that character has personally done.  If you are playing
  an npc (one of the freebie characters) the level instead of the score
  will be displayed.  Right above these numbers should be something
  like "SPC: CHARGE".  This displays your currently-selected special
  ability.  If it prints in yellow, like the others, you can perform
  this action.  If it's red, you are unable to do this special right
  now, most often because you don't have the required magic points.

  The numbers following "TEAM" and "FOES" in the upper right are the 
  number of members left alive on your team, and the number of foes 
  left, respectively.

  Finally, the lower right corner will be filled with a radar map.
  This map shows a larger portion of the playing area than can fit
  on a screen, and can help you determine where enemies and allies
  are.  Your team members are shown as red dots; other teams are shown
  as dots of their respective colors.  Weapons, explosions, and other
  things will often show up briefly as grey dots.

  After hitting ESC to exit the introductory text, the game will begin.
  Gladiator uses an angled, mostly overhead view.  You will see your
  character standing on a field of grass or dirt, perhaps with some
  trees, cobblestone, or water nearby.  Everybody on your team is
  wearing red; the bad guys will wear a variety of colors depending on
  their team (the blue team, the yellow team, etc.).  Normally, when
  you fire, your shots will pass over your teammates and hit your
  enemies; use this to your advantage!

  When you begin Scenario 1, you are a young recruit in a small band
  of warriors.  You, personally (at the keyboard) however, may be
  controlling "Commander."  Commander is an NPC (Non-Player-Character),
  which in this case means that although he is on your team, you are
  not able to buy stats for him or examine him when you are in the
  picker, and he may choose to leave at any time.  There are pro's and
  con's to having NPCs in your group:  they are often somewhat 
  stronger than your men, and can help keep you alive against the odds,
  but they DO NOT EARN EXPERIENCE, and will not go up levels, nor can
  you buy their stats up.  For this reason, it is often good to let
  the NPCs control themselves, and go off hunting experience with your
  own men.  You can tell you are controlling an NPC rather than a
  normal character because the score panel will display his level
  rather than his experience.

  For this scenario, switch characters (press numeric "+") several times
  until you get out of the officers and into the enlisted ranks --
  your personally bought men.  Try out your ranged attack, if you
  have one, by pressing the fire key (INSERT), while staying away from
  enemies.  Try hitting your special key (ENTER) and see what happens.
  Later, when you have more special abilities, you can select them
  with your 'special cycler,' such as numeric "-".  Finally, if
  you've mastered the art of moving around and firing, head up north
  and look for the nasty elves who have been killing the local travelers.

  Don't be too upset if all your men are slaughtered and you lose the
  scenario. There's NO penalty; all that will happen is that you will
  be returned to the picker.  From here, choose "CONTINUE GAME," and
  then just hit the "GO" button to try it again.

  Assuming you're able to overcome the odds and kill all the bad guys
  (the "FOE" count will drop to zero), it's time to look for the exit
  to the next level, which in this scenario is near the top.  Exits
  look like four glowing arrows pointing at each other.  Most often,
  exits won't function until you have killed all the enemies, 
  although in some special cases you can flee while foes are still
  alive (if you step on an exit and nothing happens, kill more foes!).
  When you've completed the scenario, a small window should appear asking
  if you'd like to go on to the next level.  Hitting "Y" will save
  your game and take you to the picker, where you can spend some more
  hard-earned cash before continuing to the next level. A "N" response
  will let you stay on the level.  It's often good to check a completed
  scenario level to see if there are any hidden treasures -- such as
  gold bars or food -- left around.

  On the first scenario, you might notice another exit in the lower
  left corner of the map; taking this exit will jump you to level 10,
  in case you know all about the game and don't need the introductory
  levels to get you started, but it's NOT a good idea to take it when
  you've just started out!

  Even when a good character dies, all is not lost.  A 'heart gem' will
  appear over the corpse of the deceased; any team member can pick it
  up and gain back the majority of what it cost to outfit and train
  that character.  While losing your team members isn't great, you
  may find yourself unable to complete a difficult level without losses,
  so don't worry too much about it.

C) Continuing the cycle

  That's it!  Every time you complete a scenario successfully, your
  current game will be saved and you'll head back to the picker.
  Sometimes there will be places you can pass back through levels you
  have already completed.  There won't be any enemies or treasure here,
  but they can serve as passages and rest-stops between areas you've
  completed and areas yet unsolved.

  Once you've learned the basics of Gladiator, try two player!  To
  toggle the number of players, just click the appropriate button
  on the main screen.  The currently active mode will be highlighted
  in red.

  If you find the game too tough -- or too easy! -- you can control
  the difficulty setting of the game from the main menu.  Clicking
  the "Difficulty" button will select from "Skirmish" (Easy),
  "Battle" (Normal), and "Slaughter" (Difficulty) modes.  In the easy
  mode enemies have fewer hitpoints and mana, but are worth fewer
  points to kill.  The difficult setting is just the opposite: enemies
  are stronger, but worth more to kill.

  The standard Gladiator multi-player system is cooperative (you're
  all on the same team), although racing for experience is fun.  Score
  is shared between the team, while experience goes to the character
  who earned it.

  For players who want to try "player-versus-player" mode, this option
  is available from the training menu.  By default, all characters
  start out on "Team 1."  If you change this setting (you can select
  teams 1 through 4), then each player will control a different team
  of characters.  In this mode, each team has its own score, which
  can be used to hire and train troops.  A character may change his
  team at any point from the training menu.

  The way teams treat each other is controlled from the main menu's
  "Player-v-Player" button.  When this setting is set to "Allied,"
  team members will not actively seek each other out as foes, although
  they may attack NPCs on player teams.  If the "Player-v-Player" setting
  is set to "Enemy," your team will consider everyone not on your own
  team as a deadly enemy.

  You may play the player-versus-player modes on the normal levels, or
  in the "300-range" levels.  To select these levels from the registered
  version, click the "Set Level" button and choose "300" to begin playing.


[4] Hints and Tips

A)  Two (or more)-player mode can really help make things easier.
    Frozen by a faerie?  Get your friend to fry him from the side.

B)  Clerics may feel weak at first, but they're great for healing
    wounded team members.  They're also great for building a 'wall'
    of power to block onrushing foes; call your teammates to you
    while you build a fortress!

C)  Don't forget to use your special!  If you've got the magic to
    burn, it's almost always worth it.

D)  If you have a cleric on your team, and you are hurt, try giving
    him a push.

E)  Don't forget to check how much it costs to train your character
    in a "level."  If she's almost there, you can be a level higher,
    a scenario sooner, without much cost.

[6] Credits
  Versions 3.5 - 3.8K programmed in Watcom C++ 10.0B by:
(In alphabetical order, and not in any way implying order of importance)
  -- Chad Lawrence
  -- Doug McCreary
  -- Tom Ricket
  -- Mike Scandizzo
  Additional Coding by Doug Ricket
  Testing by:
  -- Kim Kelly
  -- Lara Kirkendall
  -- Lee Martin
  -- Karyn McCreary
  -- Doug Ricket
  -- Stefan Scandizzo
  -- Loki, Ishara, and Mootz (well ... they tested the keyboard, anyway) :)

  Originally designed in Borland C++ 4.02
  
  See AUTHORS file for Openglad contributors.



Gladiator (not Openglad) revision history:
==========================================

For release 3.8K:  (09/04/98)
 * Fixed 'friendly-ghost-scare' bug
 * Added patch for overflowing team members (extra characters
   will now be randomly scattered over the map)
 * Fixed Druid 'reveal' ability
 * Made exits and teleporters always show up on radar
 * Added Barbarian's first special: Hurl Boulder, an ability based
   on strength
 * Added Barbarian's second special: Exploding boulder

For release 3.8J: (08/15/97)
 * Fixed bug in 'friendly' finding; helped cleric heal, etc.
 * Added 'Hire for Team #' button hiring menu, to make
   multi-player gaming easier

For release 3.8I: (08/13/97)
 * Fixed "walking in circles" bug in AI
 * Increased number of user-possible levels to 500 from 200
 * Added initial Player-v-Player mode
 * Implemented allies and enemies for player-v-player mode
 * Added 'Par Value' setting to scenarios
 * Added another 5 save-game slots, for a total of 10
 * Made the system load from file before trying the pack file,
   which should make user-designed levels and graphics easier

For release 3.8H: (09/03/96)
 * Removed "Knives now X" message
 * Fixed (?) painful returning knives for soldiers
 * Fixed key GPF in shareware version
 * Restored money/eating sounds to shareware version
 * FLIGHT no longer works over walls (ie, Faeries) .. only
   ethereal (ie, Ghosts) can go through walls.
 * Changed difficulty names
 * Fixed wrapping bug in scenario editor
 * Added "Save level first?" prompt to scenario editor
 * Added "/?" commandline to scen, added bad dir notification
 * Improved user I/O and info messages in scenario editor
 * Added scenario titles to game and editor
 * Fixed overlapping faerie bug in Druid spell
 * Changed description of Barbarians, etc., in team view (was 'beast')
 * Added ArchMage details information & highlighted details topics
 * Fixed names of Orc Captain through Tower in score panel
 * Fixed problem of speed not properly increasing with DEX
 * Added 'Turn Undead' entry to classes.txt file
 * Fixed character/border overlap problem

For release 3.8G: 
 * Fixed several radar colors
 * Finally added "Yell key" to readme.txt and online help
 * Added 'speed-cheat' to registered version
 * Added speed-enhancing potions
 * Added potion-type notification message
 * Fixed problem in change-team cheat
 * Added secondary level-1 Cleric spell, Mystic Mace
 * Added right mouse-button support for training/purchasing troops
 * Reinstated keyboard shortcuts into Picker
 * Added 'guard-tower' system
 * Fixed bouncing-rock GPF (thanks, Nick!)
 * Added thief charm opponent (3rd level alternate)
 * Make cleric behave with special / alternate
 * Fixed GPF & bug of multiplying knives
 o need to update README to
   - not include REGISTER info in registered version
 * update scenarios to have speed potions, towers, etc.

For release 3.8F:
 * Fixed GPF for displaying character stats when no "shots"
   have been fired
 * Fixed GPF in "transform_to" when setting the frame number
 * Removing "domain error" messages
 * Implemented "difficulty settings"
 * Fixed bug of allowing player to start a new game/level
   without any troops :)
 * Removed 2 "cross-link" buttons under "View Team" to
   prevent menu-looping
 * Fixed assorted bugs/quirks in ArchMage charm/mind-control
 * Added levels 35-38, 80-85 for testing purposes

For release 3.8E:
 * Mostly internal fixes, not visible to user.
 * Upgraded shipping copy of SCEN.EXE so that walls
   and carpets are 'auto-smoothed'
 * Added ArchMage 'Charm Enemy' Spell
 * Added a few more levels (still in testing)
 * Upgraded SCEN.DOC information file
 * Fixed problem with 'small carpet' passability

For release 3.8b:
 * Added a test button under the continue menu, to allow
   easier testing of user scenarios.  Send us your 
   scenarios!  This button will also allow registered
   users to skip to any level in the normal game
   (eliminating those annoying treks to level 10)
 * Fixed the door crash
 * Fixed vanishing keys problem (could lock out some
   level solutions)
 * Minor AI changes, still not performing as expected

For release 3.7h:
 o Fix placement of explosions (add_ob,..., 3)
 o Fix 'shadow doors' left after opening
 * Added Key-&-Door system :) At last!
 * Set ArchMage 'Summon Elemental' to require 150+ Int
 * Modifed Magi teleport so that if you're close to or on your
   marker, you'll teleport randomly, not back to the marker
 * 'Debounced' special-switch key; won't cycle automatically now
 * Fixed some problems with resetting keyboard after joystick mode
 * Fixed bug with summoned-monsters not dying when their owner did
 * Modified ArchMage's 'Summon Elemental' to have a drain on the mage :\
 * Increased performance of weapon memory allocation & use
 * Fixed sliding to work with narrow entrances, etc.
 * Fixed problem with 'slow firing' for extremely dextrous characters
 * Added 'background buttons' to floating text display as option to
   improve legibility
 * Added Golem with boulder to registered version

For release 3.7g: (09/01/95)
 * Fixed EXP assignment for summoned monsters (via weapons)
 * Added 'Summon Image' and 'Summon Elemental' to Archamge (registered)
 * Integrated Mike's current code (screen, view)
 * Fixed old 'exponential cleric EXP' bug to be in line with other classes
 * Fixed loss of 'Combat Stats' for resurrected characters
 * Modified mana penalty for resurrected characters
 * Added Treehouse, elf-generator
 * Fixed endless-owner cycle, which would freeze the game :(
 * Improved ArchMage AI somewhat
 * Fixed some joystick problems
 * Retains joystick settings between sessions

For release 3.7f: (08/26/95)
 * Added 'Details' Button, & ability to transform to a higher
   character type (ie, mage -> archmage)
 * Fixed 'overlapping' bug in Mage Teleport-to-marker spell
 * Added cheat for character-editing in picker :>
 * Added ArchMage sub-class to registered version
 * Added auto-sight for ArchMage
 * Re-coded screen::add_ob and associated routines for faster speed.
 * Modified ArchMage fireball to be dependent on current spellpoints
 * Restored 'teleport' to ArchMage
 * Added 'HeartBurst' as ArchMage 2nd level special
 * Added 'Chain Lightning' as ArchMage 2nd alternate :)

For release 3.7e: (08/19/95)
 * Made invisible enemies invisible on radar, at last
 * Increased strength of Archer Exploding Bolts, except to attacker
 * Increased potency and duration of Thief poison cloud
 * Gave Barbarian a hammer for long-range weapon
 * Increased potency of Thief Taunt
 * Added 'HeartBurst', 5th-stage Mage special :>

For release 3.7d: (08/16/95)
 * Brightened all button colors one notch, per Dark's request
 * Added 'Exp/Kill', 'Damage/Hit', & 'Accuracy' to troop records
 * Fixed 'heart gems' to be based on stats, rather than exp

For release 3.7c: (08/14/95)
 * Added ability to differentiate between magical and normal attacks
   (ie, slime is more vulnerable to magic now)
 * Moved weapon's death properly to weapon object :)
 * Added bouncing rocks ability to Elves, specials 2-4
 * Fixed display bug with slime costs in picker
 * Modified Slime breeding problem, again
 * Added snapshots and fleshed out intro-sequence
 * Added Barbarian and Orc Captain (Orcer) classes to registered version
 * Raised stat costs slightly
 * Modified generators to explode upon death
 * Added Dark's revamped scenarios

For release 3.7b: (08/12/95)
 * Took out graphics-subdirectory portion of the setup routine
 * Made gamma settings hold across levels
 * Placed 'Exp' above bar on training menu
 * Fixed 'flashing' on save/load menu in picker
 * Stopped generators from generating during NULL-TIME, and fixed
   printed 'Time Left' message to be friendlier
 * Fixed 'Train Troops' menu to allow backwards-cycling past the
   first team member
 * Fixed 'View Team' to display only up to MAXTEAM, which has been
   reset from 40 to 24
 * Made mouse 'flicker' over animated guy, instead of vanishing :)
 * Fixed graphic display bug of hitting '-' to make a stat look
   lower than it was allowed to be
 * Improved the implementation of Elven 'ForestWalk' ability
 * Fixed minor bug in right-hand-rule walking, and added check for
   forward-right-blocked
 * Made scenarios use packed-file format
 * Added ability to rename characters once they are purchased
 * Adjusted a misbehaving slime on level 4.
 * Changed start of level so players do NOT start in control of
   NPC's or each other, unless there's no other choice.
 * Added scenario 30 (orc attack) to registered version

For release 3.7a: (08/09/95)
 * Fixed level bug forcing withdrawal from completed, dead-end levels
 * Improved graphic display on save/load menus in picker
 * Added initialization info to initial loading screen
 * Removed annoying flashing screen at start of picker loading
 * Replaced graphical button for 'Begin New Game', different shading
 * Changed 'Buy Team' to 'Hire Troops'
 * Made the cost of a selected member turn red when too high to buy/change
 * Wrote the register.tex file
 * Added a larger font, and added it to dialog-box headers
 * Added text::query_width for use in proportional spacing
 * Fixed text display bug; team member deaths now show by name
   (ie, "Bleck Died!" rather than "Mage Died!")
 * Made text auto-replace initial highlighted text if user begins typing
   (rather than delete, return, etc.)
 * Fixed 'Sudden Slime Death Syndrome' problem
 * Added 'Kills' recording to glad; keeps track of # enemies killed,
   and their average level
 * Integrated DougR's packed-file format and implemented; MUCH speed
   increase! :)
 * Added DougR to the credits for the packfile coding


For release 3.6k: (08/06/95)
 * Fixed bug in thief poison cloud which crashed Glad! (oops)
 * Fixed graphic display bug in red-button drawing routine
 * Fixed 'forestwalking weapons' etc. graphic display bug
 * Added animated figure to 'Buy Team' menu, all modes
 * Added highlighted stat values to Train and Buy menus to
   indicate changed values during purchase/appraisal
 * Added video::draw_dialog function for easy text display
 * Added confirmation box for quitting when user hits ESC during
   gameplay
 * Fixed graphic display bug in setting key-prefs menu
 * Added ability to display 'alternate' special, such as teleport
   marker or turn undead
 * Improved graphic look-&-feel for completing and aborting
   scenarios
 * Fixed AI bug in Archer's retreat intelligence
 * Took out debugging print for joystick on quitting
 * Fixed bug that caused ghost to scare team-mates, as well
 * Made explosions not shove targets outrageously at high levels
 * Fixed 'fleeing bonepile' amusing bug
 * Modified intro screen to use different graphics for title, etc.
 * Added 'register me!' info page to unregistered version

For release 3.6j: (08/02/95)
 * Added indented text bar, used for 'ESC TO CONTINUE'
   messages on help/scenario text boxes
 * Fixed minor bugs in Druid 'Protection'
 * Added Elven 'Forestwalk' innate ability
 * Fixed screen::act to properly delete 'fx' object when dead
 * Added Druid 'Reveal' to Registered version
 * Fixed Cleric Resurrection to properly take an EXP toll
 * Added Cleric 'Turn Undead' as SHIFTER+[Raise Dead] to all
 * Added graphics buttons and background to Picker, all versions

For release 3.6i: (07/31/95)
 * Improved scrolling on help/scenario information..
 * Added 'Gained Level' notification to end of level info
 * Added Thief 'Taunt' special to Registered version
 * Added Archer 'Exploding Bolt' to Registered version
 * Added Soldier 'Whirlwind' to Registered version
 * Added Thief 'Poison Cloud' to Registered version
 * Added Orc 'Eat Corpse' to all versions
 * Added new ability notification to end of level info
 * Added Mage 'Teleport Marker' to all versions, 75+ Int.
 * Added Enemy death messages for named (special) enemies
 * Added Soldier 'Disarm' to Registered version

For release 3.6h: (07/14/95)
 * Added configurable keys for each player
 * Stabilized play under QEMM and other memory managers
 * Improved 'View Team' during team management
 * Integrated intro sequence and added names to credits
 * Colored picker mages red, at last
 * Fixed 'resurrection' treasure bug
 * Added scenarios 41-45 for both versions
 * Smoothed text scrolling for scenario info
 * Fixed problems with 'remembering' screen size between levels

For release 3.6: (06/03/95)
 * Added 3 and 4-player support
 * Added specialized graphic effects for potions, etc.
 * Added 4 additional scenarios to registered version
 * Updated help info
 * Added text coloring
 * Improved AI
 * Adjusted special costs
 * Display current special name on-screen
 * Changed soldier attack to limited 'bounce' weapon
 * Added soldier boomerang to registered version
 * Added mage freeze time to registered version
 * Added orc (with Howl) to registered version
 * Added assorted sounds to registered version
 * Added time bonus (for fast level completion) to registered version
 * Added 'life gems'
 * Reformulated level/exp system to allow for higher levels
 * Reformulated scenario editor for inclusion with registered version
 * Added assorted missing symbols to text
 * Added options menu for control of environment
 * Fixed 'death by asphyxiation' bug on non-square levels
 * Limited Cleric's glow time
 * Fixed assorted memory leaks in picker
 * Added ability to name guys when purchasing
 * Vastly stabilized picker team-list code :) (we hope)
 * Made death gems appear on radar
 * Adjusted level cost for new versus 'trained' guys

Since release 3.0: (05/23/95)
 * Ported from Borland to Watcom
 * Integrated Picker and Glad executables
 * Added sound
 * 32-bit processing and graphics for better performance (dos4gw flat mode)
 * Full Screen and mini screen modes
 * Moved score panel functionality onto viewscreen
 * Added completed druids to freeware version
 * Invisibility potions added to freeware
 * Faster collision detection with two dimensional hashing
 * Ghost's fear-aura works
 * Added numerous viewscreen preference toggles
 * Added teleporters to freeware version (on a limited basis)
 * Added 3 additional scenarios to freeware version
 * Max Graphics performance at 300 fps (486DX2/66+VLB) (F5 if you are curious)

Since release 2v: (04/20/95)
 * Implemented 'exit' objects into the picker, scenario editor,
   and Gladiator :)
 * Modified Save game format to allow NPC's to have names
 * Made the 'XXX KILLED' display use an NPC's name, if appropriate
 * Re-coded radar::draw for better speed & readability
 * Improved the smoothing in the scenario editor, and added final
   tree edges

Since relase 2u: (04/18/95)
 * Added 'myself' pointer to walkers to help trace down GPFs
 * Fixed walker::transform_to() to correctly remove itself from
   the object map before changing size; apparently helped fix
   bugs :)
 * Added screen::damage_tile() to give ability for tiles to
   be broken.  Tested by having thief bomb damage grass.
 * Added virtualized weap::animate() to weap to allow for faster
   weapon animations, and to fix GPF when ani_type was somehow
   getting set to non-zero (2, in this case)
 * Fixed stats::hit_response() to stop weapons (any non-livings)
   from responding ..
 * Added druid as real character
 * Added magic and invulnerability potions
 * Added walker::center_on(target) to center us on target ..
 * Added screen::find_foes_in_range(), and used it to improve
   the AI..
 * Fixed display bug in magic-point display bar
 * Added 'level' display to non-guy walkers
 * Added living::set_difficulty(whatlevel) to more intelligently
   set the difficulty of non-guy walkers
 * Adjust damage weapons inflict based on their owner's level
 * Set walker's damage to an int instead of a char

Since release 2t: (04/17/95)
 * Fixed bug with eating treasures
 * Added 4th level cleric spell: resurrect team members
 * Added 'effect' object for special effects..
 * Added 'scare' effect for ghosts
 * Added 'bomb' effect for thieves + explosion effect
 * Overloaded screen::add_ob() to take a ,1 as the last argument,
   specifying add to TOP of the list (so object displays below
   everything else on the oblist layer)

Since release 2s: (04/15/95)
 * Added help system and added glad.glp
 * Modified scenarios to version 3, which includes scenario text
   (edit with T), and display at the start of glad
 * Guys now automatically attack when you bump an enemy
 * Removed bloodspots for ghosts and skeletons, and made the
   cleric summon ghosts or undead based on special level

Since release 2r: (04/13/95)
 * Added help system with 'h' (reads glad.hlp)
 * Glad takes parameter ("1" or "2") to determine number of players
 * Added number of players selection to picker.
 * Added 'current_special' and made specials level-dependent
 * Added experience to guys and made levels dependent on this

Since release 2q: (04/10/95)
 * Added death() function to walker
 * Made slimes die to smaller slimes, removed blood spot for fire elem.
 * Modified dying (generate_bloodspot) to move walkers to the
   fx layer, and mark them as bloodspots
 * Used new system to allow clerics to 'raise dead' from any bloodspot
   (temporarily removed healing option)
 * Added new terrain types (paths, cobblestones, etc.)
 * Made fire elementals explode upon death
 * Set shrinking bloodspots to not collide
 * Converted walker class to derived types (living, weapon, treasure)
 * Fixed food-eating problem with treasure objects
 * Made score panel display guy name
 * Added gold and silver bars
 * Added magic potions, ie flight

Since release 2p: (04/07/95)
 * Re-added the floating text to viewscreen(s)
 * Multiplied graphics size by four times ..
 * Improved graphic display window to allow for non-integer
   viewport display width/height

Since release 2n: (04/05/95)
 * Added animation for large slime splitting
 * Fixed HP/SP color bar color problem

Since release 24: (04/01/95 Interim release)
 * Added multiple viewscreens for multi-player mode
 * Added mapped keyboard controls for use in multi-player mode
 * Broke the screen object into a graphics, screen, and viewscreen modules
 * Added 'right-hand-rule' based movement for AI
 * Added transformation function (ie, for slime) and associated cheat-key
 * Reworked slimes and set up full-cycle of growth/replication

Since release 2L: (3/25/95)
 * Added NO_RANGED bit-flag, and used it to make the small
   slimes have no ranged attack (needs to be fixed)
 * Add new bit-flags INVINCIBLE, can't be hurt :) (+ cheat key)
 * Added default and current weapon types, and commands
   set_ and reset_ weapon .. used this to get flaming arrows for
   the archer's special
 * Added 'armor' to the gameplay (subtracts damage done to you)
 * Made magic regeneration based on intelligence
 * Added hitpoint regeneration based on constitution
 * Added treasure 'food' item; can be eaten for hitpoints
 * Added gamma correction (ALT + NUMERIC PLUS, MINUS, & ENTER for default)
 * Integrated with picker better

To do: since 2k (03/23/95)
 * Recompiled in protected mode for bug-fixing
 * Ignore (normal) keyboard input if command-queue exists
 * Find what is crashing glad sometimes (appears to be a NEW, not
   old bug) (apparently was deleting a non-existant radar)
 * Put guy/object display on radar (though not to 'scale')
 * Make the radar bigger (50x50?) (40x40 looked better)
 * Make a nifty graphics border for the radar
 * Add slimes eventually; they have a slime ball ranged attack
 * Make loader use the team list..
   - Now loads a GTL (saved game) file! At last! :)
 * Added colors.h for easy color reference
 * Added Win and Lose cheat keys
 * Winning a level now saves game to savetemp.gtl
 * Put in slime specials: small => medium => large

Since release 2i,j: (3/19/95)
 * Improved movement -- better baby-stepping, diagonals
 * Implemented command 'queue' for better AI
 * Added specials to archer and footman
 * Implemented radar (background only)
 * Implemented full-turning system with single keystroke
 * Implemented fire 'cost' (of MP); can see for mage's fireballs
 * Added diagonal graphics for mage

Since release 2g, h: (3/10/95)
 * Speed enhancements to various graphic routines
 * Code integration of main files with picker, scenario editor
 * Fixed up 'yo' to have delay; deal with foes better
 * Ready for transfer to Mike for now :)

Since release 2f:
 * Made game speed adjustable using numeric PLUS and MINUS

Since release 2e:
 * Faster walkputbuffer
 * Fixed shove bug
 * Optimized auto-walking
 * Cleaned out old error detection code
 * Corrected most warnings

Since release 2d: (3/07/95)
 * Rounded MP/HP display
 * Added slime-type guy (not pictured here)

Since release 2c:
 * HP & Magic point bar display
 * More cheat codes (Flying, Magicpoints, Hitpoints)
 * 'Incremental' movement when blocked

Since release 2b:
 * Can now only push members of same team
 * Implemented slippery walls :)
 * Replaced red flash with fast blood splash
 * Improved speed of screen redraw by about 30%
 * Improved AI on diagonals
 * Added 'yell' command to summon teammates (Y)

Since release 2a:
 * Fixed 'Unknown Blaster Option X' line
 * More 'circular' weapon range

Improvements since Release 2.0:
 * Number of foes remaining display
 * Pause key (P)
 * Red-shifted palette during menus
 * Sped up system (faster horizontal movement)
 * Guys rotate through turns
 * Red flash to indicate damage to controller


Improvements since version 1
  * diagonal movement
  * improved keyboard response
  * larger viewport (with faster rendering)
  * floating text for information
  * more opponent teams
  * new character classes
  * improved maps and scenarios
  * blood splots ;) (it's gorier than doom)
  * animated tiles (flowing water, flickering torches)
  * improved AI
  * better scoring system
  * improved stability (we found some bugs ;)
  * nifty cheats (try and find them)
  * more colorful palette


