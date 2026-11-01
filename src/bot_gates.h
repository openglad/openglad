// src/bot_gates.h — the bot gates' shared predicates (port of the current
// game's packs/core/lib/gates.lua). Draw-free: finders and positions only, so
// a setting-off run is untouched. Each arm of living::check_special and the
// archmage hit response call these behind myscreen->gameplay_fixes.
#ifndef BOT_GATES_H
#define BOT_GATES_H
#include "walker.h"
#include "screen.h"
#include "guy.h"
#include <list>
#include <cstdlib>

static const int BLOW_GAP = 5;
static const int BLOW_SCAN = 80;
static const int RING_SCAN = 400;
static const int BOT_FAR = 30000;
// A bolt whose lastx / lasty the cast sets to +-1 after the fire (WARP
// SPACE's ring, walker.cpp) moves one pixel a tick on every heading: its
// step is 1 and a straight heading does not scale it. No fired weapon of
// the table has a step of 1.
static const int BG_UNIT_STEP = 1;

// The edge-to-edge gap on each axis, negative where the boxes overlap.
static inline void bg_gap(walker *a, walker *b, int *gap_x, int *gap_y)
{
	*gap_x = b->xpos - (a->xpos + a->sizex);
	if (a->xpos - (b->xpos + b->sizex) > *gap_x)
		*gap_x = a->xpos - (b->xpos + b->sizex);
	*gap_y = b->ypos - (a->ypos + a->sizey);
	if (a->ypos - (b->ypos + b->sizey) > *gap_y)
		*gap_y = a->ypos - (b->ypos + b->sizey);
}

// port of the current game's 95ae7e12: with the Gameplay Fixes setting on,
// FREEZE TIME's cue is the fight the allies are in, not the mage's own
// danger (TELEPORT's business): three or more foes within a blow of allies
// other than the caster, or an ally at blows under a quarter of its health.
// A blow lands across a gap of at most 5 px on each axis: the 6 px weapon
// box set 1 px outside the attacker reaches gaps 1 to 6, and collide()
// moves the foe's near edge in by 1 px, so 6 - 1 = 5.
// Draw-free: finders and positions only, so a setting-off run is untouched.
// (was at_blows in living.cpp, 9b8d4503)
static inline bool bg_at_blows(walker *a, walker *b)
{
	int gap_x, gap_y;
	bg_gap(a, b, &gap_x, &gap_y);
	return (gap_x <= BLOW_GAP && gap_y <= BLOW_GAP);
}

// The living foes within range (a generator is never one). The old game
// has one floor.
static inline std::list<walker*> bg_living_foes(walker *self, Sint32 range)
{
	short n = 0;
	std::list<walker*> found = myscreen->find_foes_in_range(myscreen->level_data.oblist, range, &n, self);
	std::list<walker*> out;
	for (auto f = found.begin(); f != found.end(); ++f)
		if (*f && (*f)->query_order() == ORDER_LIVING)
			out.push_back(*f);
	return out;
}

static inline bool bg_foes_within(walker *self, int n, Sint32 range)
{
	return (int) bg_living_foes(self, range).size() >= n;
}

// Generators included (fix K: WHIRLWIND strikes one, HOWL stuns one).
static inline bool bg_targets_within(walker *self, int n, Sint32 range)
{
	short count = 0;
	myscreen->find_foes_in_range(myscreen->level_data.oblist, range, &count, self);
	return count >= n;
}

static inline walker *bg_nearest_living_foe(walker *self, Sint32 range)
{
	std::list<walker*> foes = bg_living_foes(self, range);
	walker *best = NULL;
	Sint32 best_dist = 0;
	for (auto f = foes.begin(); f != foes.end(); ++f)
	{
		Sint32 d = self->distance_to_ob(*f);
		if (!best || d < best_dist) { best = *f; best_dist = d; }
	}
	return best;
}

static inline std::list<walker*> bg_allies(walker *self, Sint32 range)
{
	short n = 0;
	std::list<walker*> found = myscreen->find_friends_in_range(myscreen->level_data.oblist, range, &n, self);
	std::list<walker*> out;
	for (auto f = found.begin(); f != found.end(); ++f)
		if (*f && (*f)->query_order() == ORDER_LIVING)
			out.push_back(*f);
	return out;
}

static inline bool bg_allies_within(walker *self, int n, Sint32 range)
{
	return (int) bg_allies(self, range).size() >= n;
}

static inline std::list<walker*> bg_foes_at_blows(walker *self)
{
	std::list<walker*> foes = bg_living_foes(self, BLOW_SCAN);
	std::list<walker*> out;
	for (auto f = foes.begin(); f != foes.end(); ++f)
		if (bg_at_blows(self, *f))
			out.push_back(*f);
	return out;
}

// engaged foes and the hurt flag: FREEZE TIME's cue (was freeze_time_wanted).
static inline void bg_allies_fight(walker *self, int *engaged, bool *hurt)
{
	std::list<walker*> allies = bg_allies(self, BOT_FAR);
	std::list<walker*> foes = bg_living_foes(self, BOT_FAR);
	*engaged = 0;
	*hurt = false;
	for (auto f = foes.begin(); f != foes.end(); ++f)
	{
		bool near = false;
		for (auto a = allies.begin(); a != allies.end(); ++a)
		{
			if (!bg_at_blows(*a, *f))
				continue;
			near = true;
			if ((*a)->stats->hitpoints * 4 < (*a)->stats->max_hitpoints)
				*hurt = true;
		}
		if (near)
			(*engaged)++;
	}
}

static inline walker *bg_ally_at_blows(walker *self, Sint32 range, walker **foe_out)
{
	std::list<walker*> allies = bg_allies(self, range);
	std::list<walker*> foes = bg_living_foes(self, range + BLOW_SCAN);
	for (auto a = allies.begin(); a != allies.end(); ++a)
		for (auto f = foes.begin(); f != foes.end(); ++f)
			if (bg_at_blows(*a, *f))
			{
				if (foe_out) *foe_out = *f;
				return *a;
			}
	return NULL;
}

static inline std::list<walker*> bg_hunters(walker *self, Sint32 range)
{
	std::list<walker*> foes = bg_living_foes(self, range);
	std::list<walker*> out;
	for (auto f = foes.begin(); f != foes.end(); ++f)
		if ((*f)->foe == self)
			out.push_back(*f);
	return out;
}

static inline walker *bg_hunted_by(walker *self, Sint32 range)
{
	std::list<walker*> h = bg_hunters(self, range);
	return h.empty() ? NULL : h.front();
}

static inline walker *bg_hurt_ally(walker *self, Sint32 range)
{
	std::list<walker*> allies = bg_allies(self, range);
	for (auto a = allies.begin(); a != allies.end(); ++a)
		if ((*a)->stats->hitpoints < (*a)->stats->max_hitpoints)
			return *a;
	return NULL;
}

static inline bool bg_hp_below(walker *w, int num, int den)
{
	return w->stats->hitpoints * (float) den < w->stats->max_hitpoints * (float) num;
}

static inline void bg_centre(walker *w, Sint32 *x, Sint32 *y)
{
	*x = w->xpos + w->sizex / 2;
	*y = w->ypos + w->sizey / 2;
}

static inline short bg_heading_to(walker *self, walker *target)
{
	Sint32 sx, sy, tx, ty;
	bg_centre(self, &sx, &sy);
	bg_centre(target, &tx, &ty);
	return self->facing((short) (tx - sx), (short) (ty - sy)); // walker.cpp:258
}

// Where a fired weapon leaves: facing(lastx, lasty) (walker.cpp, fire).
static inline short bg_fire_heading(walker *self)
{
	return self->facing((short) self->lastx, (short) self->lasty);
}

static inline bool bg_has_heading(walker *self)
{
	return (self->lastx != 0.0f || self->lasty != 0.0f);
}

static inline bool bg_faces(walker *self, walker *target)
{
	return bg_heading_to(self, target) == bg_fire_heading(self);
}

static inline bool bg_aimed_at(walker *self, walker *target)
{
	return bg_has_heading(self) && bg_faces(self, target);
}

static inline bool bg_fan_faces(walker *self, walker *target)
{
	short h = bg_heading_to(self, target);
	int d = bg_fire_heading(self);
	return (h == d || h == (d + 1) % 8 || h == (d + 7) % 8);
}

static inline bool bg_straight(short heading)
{
	return (heading == FACE_UP || heading == FACE_RIGHT || heading == FACE_DOWN || heading == FACE_LEFT);
}

// Per-axis reach of a fired weapon (table step / los) along heading, as
// walker::fire shapes it (walker.cpp:2455-2480): a hero adds
// strength / 23 + dexterity / 31, every caster level / 3, a straight shot
// scales los by 309 / 256 (integer) and the step by 362 / 256 (the old
// game's stepsize is a float too). The first walk only turns the bolt.
static inline int bg_flight_px(walker *self, int step, int los, short heading,
                               int los_num, int los_den, int los_add)
{
	int steps = los;
	if (self->myguy)
		steps += self->myguy->strength / 23 + self->myguy->dexterity / 31;
	steps += self->stats->level / 3;
	float px = (float) step;
	if (bg_straight(heading))
	{
		steps = steps * 309 / 256;
		if (step != BG_UNIT_STEP)
			px = (px * 362.0f) / 256.0f;
	}
	if (los_num)
		steps = steps * los_num / los_den;
	steps += los_add;
	return (int) ((float) (steps - 1) * px);
}

static inline bool bg_in_flight(walker *self, walker *target, int step, int los,
                                int los_num, int los_den, int los_add)
{
	short heading = bg_heading_to(self, target);
	int reach = bg_flight_px(self, step, los, heading, los_num, los_den, los_add);
	int gap_x, gap_y;
	bg_gap(self, target, &gap_x, &gap_y);
	return (gap_x > gap_y ? gap_x : gap_y) <= reach;
}

static inline bool bg_ring_pays(walker *self, int step, int los, int los_add)
{
	std::list<walker*> foes = bg_living_foes(self, RING_SCAN);
	int in_ring = 0;
	bool unfaced = false;
	for (auto f = foes.begin(); f != foes.end(); ++f)
	{
		if (!bg_in_flight(self, *f, step, los, 0, 1, los_add))
			continue;
		in_ring++;
		if (!bg_faces(self, *f))
			unfaced = true;
	}
	return (in_ring >= 2 || (in_ring == 1 && unfaced));
}

static inline int bg_blast_reach(int level)
{
	int range = level * 4;
	if (range < 16) range = 16;
	if (range > 96) range = 96;
	return 15 + range;
}

// No ally of the CASTER within reach of the centre (which may be a foe):
// the caster's friends, sifted by their distance to the centre.
static inline bool bg_blast_clear(walker *self, walker *centre, Sint32 reach, bool check_self)
{
	if (check_self && centre != self && self->distance_to_ob(centre) <= reach)
		return false;
	Sint32 scan = reach;
	if (centre != self)
		scan += self->distance_to_ob(centre);
	std::list<walker*> allies = bg_allies(self, scan);
	for (auto a = allies.begin(); a != allies.end(); ++a)
		if (centre->distance_to_ob(*a) <= reach)
			return false;
	return true;
}

// The live objects of order / family the caster owns: oblist for an FX or
// living order (add_ob puts effects there), weaplist for a weapon.
static inline int bg_owned(walker *self, char order, char family, walker **first)
{
	std::list<walker*>& list = (order == ORDER_WEAPON)
	                           ? myscreen->level_data.weaplist
	                           : myscreen->level_data.oblist;
	int count = 0;
	if (first) *first = NULL;
	for (auto e = list.begin(); e != list.end(); ++e)
	{
		walker *ob = *e;
		if (ob && ob->query_order() == order && ob->query_family() == family
		        && !ob->dead && ob->owner == self)
		{
			count++;
			if (first && !*first) *first = ob;
		}
	}
	return count;
}

static inline walker *bg_owned_phantom(walker *self)
{
	for (auto e = myscreen->level_data.oblist.begin(); e != myscreen->level_data.oblist.end(); ++e)
	{
		walker *ob = *e;
		if (ob && ob->query_order() == ORDER_LIVING && !ob->dead && ob->owner == self
		        && ob->stats->max_hitpoints == 1)
			return ob;
	}
	return NULL;
}

static inline walker *bg_corpse_underfoot(walker *self, Sint32 reach_sq)
{
	walker *blood = myscreen->find_nearest_blood(self);
	if (!blood) return NULL;
	return (self->distance_to_ob_center(blood) <= reach_sq) ? blood : NULL;
}

static const int BG_STEP_X[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int BG_STEP_Y[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// The first of foes whose box, shrunk 1 px on every side as collide()
// shrinks it, meets the box at (ax, ay) of size aw x ah (already shrunk);
// collide() compares inclusively.
static inline walker *bg_foe_meeting(std::list<walker*> &foes, int ax, int ay, int aw, int ah)
{
	for (auto f = foes.begin(); f != foes.end(); ++f)
	{
		walker *w = *f;
		int fx = w->xpos + 1, fy = w->ypos + 1, fw = w->sizex - 2, fh = w->sizey - 2;
		if (ax <= fx + fw && ax + aw >= fx && ay <= fy + fh && ay + ah >= fy)
			return w;
	}
	return NULL;
}

// collide()'s own compare (both boxes shrunk 1 px a side, inclusive) on
// the caster's box moved 1 px along curdir.
static inline walker *bg_ahead_blocked_by_foe(walker *self, Sint32 reach)
{
	int d = self->curdir;
	if (d < 0 || d > 7) return NULL;
	int ax = self->xpos + BG_STEP_X[d] + 1, ay = self->ypos + BG_STEP_Y[d] + 1;
	int aw = self->sizex - 2, ah = self->sizey - 2;
	std::list<walker*> foes = bg_living_foes(self, reach);
	return bg_foe_meeting(foes, ax, ay, aw, ah);
}

// port of the current game's 31752d32: the living foe a rush along the
// caster's heading runs into. CHARGE queues COMMAND_RUSH for three acts,
// and each act walks three steps along lastx/stepsize, lasty/stepsize
// (walker.cpp, truncated into the command's shorts) and strikes and shoves
// the body it ran into (stats.cpp). The first living foe whose box meets
// the caster's box moved k such steps, k = 1 .. steps (collide()'s shrink),
// or NULL; NULL with no heading, where the rush walks nowhere. A wall, an
// ally, a door or a generator before the foe refuses, and so does a front
// the cast itself finds blocked (port of the current game's 9953b30b,
// Gameplay Fixes V).
static inline walker *bg_rush_strikes(walker *self, int steps)
{
	// The cast refuses PATH BLOCKED when the one-pixel step along curdir is
	// blocked (a wall, any body, a door): the same question, asked quietly
	// (Gameplay Fixes U); a foe already pressed against the front is a blow,
	// not a charge.
	if (self->stats->forward_blocked(true))
		return NULL;
	float step = self->stepsize;
	int ux = (int) (self->lastx / step);
	int uy = (int) (self->lasty / step);
	if (ux == 0 && uy == 0) return NULL;
	int span = abs(ux) > abs(uy) ? abs(ux) : abs(uy);
	int run = (int) ((float) (steps * span) * step);
	std::list<walker*> foes = bg_living_foes(self, 2 * run + BLOW_SCAN);
	int aw = self->sizex - 2, ah = self->sizey - 2;
	for (int k = 1; k <= steps; k++)
	{
		int ax = self->xpos + (int) ((float) (ux * k) * step) + 1;
		int ay = self->ypos + (int) ((float) (uy * k) * step) + 1;
		walker *w = bg_foe_meeting(foes, ax, ay, aw, ah);
		if (w) return w;
		// Nothing struck here: the rush must be able to stand here on its way.
		// A wall, an ally, a door or a generator at this step stops it before
		// any later foe (COMMAND_RUSH strikes whatever its walksteps bump into).
		if (!myscreen->query_passable(ax - 1, ay - 1, self, true))
			return NULL;
	}
	return NULL;
}

// port of the current game's 9953b30b (Gameplay Fixes V) and 6ae833d7: the
// pad fire() would place the caster's weapon on is clear: the weapon would
// fly instead of meeting a wall face, the map edge, a tree crown (for a
// weapon that does not walk the forest) or a foe's body there
// (walker::fire_pad_clear, the same spot and the same weapon rules, asked
// quietly). Every weapon crosses water. A body of the caster's own team
// passes, as the weapon passes it; a door, a grown tree or a weapon in
// flight on the pad is passed by the weapon probe too. weapon -1 probes the
// current weapon; a cast that fires another one passes its family.
static inline bool bg_pad_clear(walker *self, short weapon = -1)
{
	return self->fire_pad_clear(weapon);
}

// The old game has no priced alternates: a Shift costs the slot's price.
static inline bool bg_can_pay(walker *self, int slot)
{
	return self->stats->magicpoints >= self->stats->special_cost[slot];
}

static inline bool bg_resting(walker *self, int slot)
{
	return (self->special_rest_slot == slot && self->special_rest_left > 0);
}

static inline bool bg_marker_pays(walker *self, int next_slot, int int_req)
{
	if (self->myguy && self->myguy->intelligence < int_req)
		return false;
	if (bg_owned(self, ORDER_FX, FAMILY_MARKER, NULL) > 0)
		return false;
	Sint32 spare = (Sint32) (self->stats->magicpoints - self->stats->special_cost[self->current_special]);
	return (spare / 2) >= self->stats->special_cost[next_slot];
}

// The archmage's slot-2 gate (port of the current game's ai_burst_or_chain),
// asked by its check_special arm and by its hit response. CHAIN LIGHTNING
// wants a foe inside its radius, HEARTBURST inside its own: the coin's arm
// is kept when it has a target, swapped when only the other has one,
// refused when neither has
static inline bool bg_archmage_burst_or_chain(walker *self)
{
	// The cast's own guard: it refuses a busy archmage (port of the current
	// game's 44a685f0, Gameplay Fixes W)
	if (self->busy > 0)
		return false;
	short in_burst = 0;
	short in_chain = 0;
	myscreen->find_foes_in_range(myscreen->level_data.oblist,
	                                        80 + 2*self->stats->level, &in_burst, self);
	myscreen->find_foes_in_range(myscreen->level_data.oblist,
	                                        200 + 5*self->stats->level + 2*self->stats->level, &in_chain, self);
	if (self->shifter_down)
	{
		if (in_chain >= 1)
			return true;
		self->shifter_down = 0;
		return (in_burst >= 1);
	}
	if (in_burst >= 1)
		return true;
	if (in_chain >= 1)
	{
		self->shifter_down = 1;
		return true;
	}
	return false;
}
#endif
