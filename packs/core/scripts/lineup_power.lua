-- core pack: lineup power — the DEFAULT fighter pricing every campaign borrows (docs/lineup-design.md C5): the shared lib's stat_power over the engine-derived stats row, registered through og.register_default_lineup so the LINEUP bands read POWER n on gladiator and on every campaign that names no pricing of its own (cookbook: docs/lua-classpacks-design.md §3).
-- It is a REGISTRAR OF ITS OWN, not a fifth key on the campaign book: that
-- book is one-campaign-one-book, and a second og.register_campaign_hooks
-- poisons the whole registration ("no scripted picker will be served"), so
-- registering the default there would have killed the picker of every
-- campaign that ships a book. The default sits in its own per-VM slot,
-- where a campaign can neither poison it nor be poisoned by it — and where
-- a campaign's own lineup.power still wins outright (the modes book keeps
-- pricing with this exact metric, so its numbers do not move).
--
-- The qualified og.use works here for the reason the modes book's does: the
-- campaign VM loads every installed pack's lib modules exactly like a world
-- VM, and stat_power spends nothing but og.div, which the campaign fence
-- leaves open.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local lineup = og.use("lineup")

-- One fighter, priced: the same f the FILL solver measures squads against,
-- so a band's POWER and the squad it faces are quoted in one currency.
local function default_power(row)
  return lineup.stat_power(row.hp, row.mp, row.armor, row.damage,
                           row.stepsize, row.fire_frequency, row.level)
end

-- The C8 resolver, registered beside the pricing so the LINEUP band's
-- knob face renders the RESOLVED default through the same lib function
-- the stage executes (lineup.resolved_fill — the ONE home of the rule;
-- the menus never re-derive it). The engine hands over the stored wheel
-- code and the team's censused presence row; explicit values come back
-- as themselves, the default resolves FAIR-with-presence / NONE-without.
local function default_fill(stored, row)
  return lineup.resolved_fill(stored, row)
end

og.register_default_lineup({ power = default_power,
                             default_fill = default_fill })
