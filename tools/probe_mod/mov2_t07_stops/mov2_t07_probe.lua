-- ==== mov2 t07: squad stop distance, re-order braking and MovingVsMoving holds in retail ====
-- tools/probe_mod/mov2_t07_stops/mov2_t07_probe.lua. build_mov2_t07_mod.py appends to the shipped
-- BusyTacticalFreeStore.lua: SN_CFG (generated), the record / engine helpers of
-- ../s78n_t14_squads/s78n_t14_probe.lua (SN_*, records renamed M7Q), this file, then that file's arming and service
-- drivers (SN_Arm / SN_Tick, which call SQ_Tick below).
--
-- Questions (roadmap mov2 t07, backlog b0093; FIGHTER_FLYING 7.7 items 4 / 5, MOVEMENT 3.5.2 C / G):
--   STOP     how far a squad container (and a plain Walk vehicle) travels from the start of its braking to rest, how
--            many frames it spends in End Move (0x14), and where it rests against its goal
--   REORDER  a new Move_To while the unit marches: (same) straight on past the old goal, (turn90) to the side,
--            (reverse) back: does it keep its speed (aligned path, MOVEMENT 3.5.2 G) or coast at its deceleration
--            (not aligned) before the new path starts, and how far / how long
--   MVM      two T4B tanks ordered head-on along one line (and with a small side offset): how long does each hold at
--            speed 0 (the port: MovingVsMoving holds of 166 / 232 frames in s78n t25.4 seeds; retail state-4 ~160 by RE)
-- The frame-exact numbers come from the runner's memory reader (mov2_t07_run.py: per logic frame position, facing,
-- locomotor state and speed of every object id these records name); the Lua records give the ids, the order frames
-- and a sparse position trace as a fallback.
-- Geometry: local (u, v) on the corridor the builder found: u along SN_CFG.sq.theta0 from SN_CFG.sq.site, v to the
-- left; the strip u 0..900, v 0..120 was checked passable and clear of map objects.

SQ = { phase = "init", ti = 0, cur = nil, next_f = nil }

function M7_World(u, v)
	local s = SN_CFG.sq
	return s.site.x + s.ux * u + s.vx * v, s.site.y + s.uy * u + s.vy * v
end

function M7_Local(x, y)
	local s = SN_CFG.sq
	local dx, dy = x - s.site.x, y - s.site.y
	return dx * s.ux + dy * s.uy, dx * s.vx + dy * s.vy
end

function M7_UV(o)
	local x, y = SN_XYn(o)
	if x == nil then
		return nil
	end
	return M7_Local(x, y)
end

function M7_Fmt(u, v)
	if u == nil then
		return "?"
	end
	return string.format("%.2f,%.2f", u, v)
end

-- the unit that takes the orders (container or vehicle) and the ids the memory reader should follow
function M7_Snapshot(names)
	local s = {}
	for _, tn in ipairs(names) do
		for _, o in ipairs(SN_FindAll(tn)) do
			local id = SN_Id(o)
			if id ~= "?" then
				s[id] = true
			end
		end
	end
	return s
end

function M7_Spawn(tname, u, v)
	local x, y = M7_World(u, v)
	local owner = SN_Player(SN_CFG.human_faction)
	return SN_SpawnList(tname, SN_Pos(x, y, SN_CFG.sq.site.z), owner)
end

function M7_Protect(o)
	SN_Call(o, "Make_Invulnerable", function() o.Make_Invulnerable(true) end)
	if SN_CFG.sq.nofire then
		SN_Call(o, "Prevent_All_Fire", function() o.Prevent_All_Fire(true) end)
	end
end

-- a squad: Spawn_Unit of the company returns its team containers; the members are the new objects of the member
-- type whose parent is the kept container; other new teams are removed
function M7_IdentSquad(t)
	local sq = t.spec
	local groups, keys = {}, {}
	for _, o in ipairs(SN_FindAll(sq.member)) do
		local id = SN_Id(o)
		if SN_Valid(o) and id ~= "?" and not t.old[id] then
			local p = nil
			pcall(function() p = o.Get_Parent_Object() end)
			local pid = "none"
			if SN_Valid(p) then
				pid = SN_Id(p)
			else
				p = nil
			end
			if groups[pid] == nil then
				groups[pid] = { pid = pid, c = p, members = {} }
				table.insert(keys, pid)
			end
			table.insert(groups[pid].members, { o = o, id = id })
		end
	end
	local best = nil
	for _, pid in ipairs(keys) do
		local g = groups[pid]
		if pid ~= "none" and (best == nil or table.getn(g.members) > table.getn(best.members)) then
			best = g
		end
	end
	if best == nil then
		return false
	end
	for _, pid in ipairs(keys) do
		local g = groups[pid]
		if g ~= best then
			for _, m in ipairs(g.members) do
				SN_Despawn(m.o)
			end
			SN_Despawn(g.c)
		end
	end
	for _, o in ipairs(t.ret) do
		if SN_Id(o) ~= best.pid then
			SN_Despawn(o)
		end
	end
	table.sort(best.members, function(a, b) return SN_NumId(a.id) < SN_NumId(b.id) end)
	t.units = { { o = best.c, id = best.pid, role = "c" } }
	local ids = ""
	for _, m in ipairs(best.members) do
		M7_Protect(m.o)
		table.insert(t.units, { o = m.o, id = m.id, role = "m" })
		ids = ids .. m.id .. ";"
	end
	M7_Protect(best.c)
	t.mover = best.c
	SN_Rec("TEAM", "k=" .. t.k .. "|name=" .. t.name .. "|cid=" .. best.pid .. "|ctype=" .. SN_Type(best.c) ..
		"|n=" .. table.getn(best.members) .. "|mtype=" .. SN_Type(best.members[1].o) .. "|ids=" .. string.sub(ids, 1, 150))
	return true
end

-- a vehicle: the new object of the type closest to the spawn point
function M7_IdentUnit(t, tname, u, v, role)
	local best, bd = nil, nil
	for _, o in ipairs(SN_FindAll(tname)) do
		local id = SN_Id(o)
		if SN_Valid(o) and id ~= "?" and not t.old[id] and not t.taken[id] then
			local ou, ov = M7_UV(o)
			if ou ~= nil then
				local d = (ou - u) * (ou - u) + (ov - v) * (ov - v)
				if bd == nil or d < bd then
					best, bd = o, d
				end
			end
		end
	end
	if best == nil then
		return nil
	end
	local id = SN_Id(best)
	t.taken[id] = true
	M7_Protect(best)
	table.insert(t.units, { o = best, id = id, role = role })
	SN_Rec("UNIT", "k=" .. t.k .. "|name=" .. t.name .. "|role=" .. role .. "|id=" .. id .. "|type=" .. SN_Type(best) ..
		"|at=" .. M7_Fmt(M7_UV(best)))
	return best
end

function M7_Start(F, k, spec)
	local t = { k = k, name = spec.name, kind = spec.kind, spec = spec, f0 = F, phase = "ident", mi = 0, errs = 0,
		units = {}, taken = {} }
	SQ.cur = t
	if spec.kind == "squad" then
		t.old = M7_Snapshot({ spec.member, spec.team })
		t.ret = M7_Spawn(spec.company, spec.at[1], spec.at[2])
	elseif spec.kind == "unit" then
		t.old = M7_Snapshot({ spec.type })
		t.ret = M7_Spawn(spec.type, spec.at[1], spec.at[2])
	else
		t.old = M7_Snapshot({ spec.a.type, spec.b.type })
		t.ret = M7_Spawn(spec.a.type, spec.a.from[1], spec.a.from[2])
		local r2 = M7_Spawn(spec.b.type, spec.b.from[1], spec.b.from[2])
		for _, o in ipairs(r2) do
			table.insert(t.ret, o)
		end
	end
	SN_Rec("TRIAL", "k=" .. k .. "|name=" .. spec.name .. "|kind=" .. spec.kind .. "|ret=" .. table.getn(t.ret))
	t.fi = F + SN_CFG.sq.ident_wait
	local cx, cy = M7_World(450, 60)
	SN_Camera(cx, cy, SN_CFG.sq.site.z)
end

function M7_Ident(t, F)
	local spec = t.spec
	local ok = false
	if spec.kind == "squad" then
		ok = M7_IdentSquad(t)
	elseif spec.kind == "unit" then
		t.mover = M7_IdentUnit(t, spec.type, spec.at[1], spec.at[2], "u")
		ok = t.mover ~= nil
	else
		t.ua = M7_IdentUnit(t, spec.a.type, spec.a.from[1], spec.a.from[2], "a")
		t.ub = M7_IdentUnit(t, spec.b.type, spec.b.from[1], spec.b.from[2], "b")
		ok = t.ua ~= nil and t.ub ~= nil
	end
	if not ok then
		SN_Rec("ERR", "what=ident|k=" .. t.k .. "|name=" .. t.name)
		t.phase = "end"
		return
	end
	-- the line the memory reader keys on: every id it should follow in this trial
	local ids = ""
	for _, x in ipairs(t.units) do
		ids = ids .. x.role .. ":" .. x.id .. ";"
	end
	SN_Rec("TRACK", "k=" .. t.k .. "|name=" .. t.name .. "|ids=" .. string.sub(ids, 1, 300))
	t.phase = "settle"
	t.fs = F
end

function M7_Order(o, u, v)
	local x, y = M7_World(u, v)
	local p = SN_Pos(x, y, SN_CFG.sq.site.z)
	if not SN_Valid(o) or p == nil then
		return "dead"
	end
	local ok, r = pcall(function() return o.Move_To(p) end)
	if not ok then
		SN_ApiFail("Move_To", r)
		return "error"
	end
	return "ok"
end

-- sparse fallback trace (every `trace_every` frames): each unit's local (u, v)
function M7_Poll(t, F, force)
	if not force and t.lastp ~= nil and F - t.lastp < SN_CFG.sq.trace_every then
		return
	end
	t.lastp = F
	local s = ""
	for _, x in ipairs(t.units) do
		s = s .. x.role .. "=" .. M7_Fmt(M7_UV(x.o)) .. ";"
	end
	SN_Rec("P", "k=" .. t.k .. "|mi=" .. t.mi .. "|ph=" .. t.phase .. "|" .. string.sub(s, 1, 400))
end

function M7_NextMove(t, F)
	local spec = t.spec
	t.mi = t.mi + 1
	local mv = spec.moves[t.mi]
	if mv == nil then
		t.phase = "end"
		return
	end
	t.mv = mv
	local u0, v0 = M7_UV(t.mover)
	local r = M7_Order(t.mover, mv.to[1], mv.to[2])
	t.fo = F
	t.ou, t.ov = u0, v0
	t.lu, t.lv = u0, v0
	t.moved, t.still, t.re_done = false, 0, false
	t.phase = "move"
	SN_Rec("ORDER", "k=" .. t.k .. "|name=" .. t.name .. "|mi=" .. t.mi .. "|move=" .. mv.name .. "|from=" .. M7_Fmt(u0, v0) ..
		"|to=" .. M7_Fmt(mv.to[1], mv.to[2]) .. "|ret=" .. r)
	M7_Poll(t, F, true)
end

function M7_MoveTick(t, F)
	local cfg = SN_CFG.sq
	local u, v = M7_UV(t.mover)
	if u == nil then
		SN_Rec("ERR", "what=no_position|k=" .. t.k .. "|mi=" .. t.mi)
		t.phase = "end"
		return
	end
	local du, dv = u - t.ou, v - t.ov
	local dl = (u - t.lu) * (u - t.lu) + (v - t.lv) * (v - t.lv)
	t.lu, t.lv = u, v
	if not t.moved and du * du + dv * dv > cfg.moved_min * cfg.moved_min then
		t.moved = true
		SN_Rec("START", "k=" .. t.k .. "|mi=" .. t.mi .. "|r=" .. (F - t.fo))
	end
	local mv = t.mv
	if mv.re ~= nil and not t.re_done and t.moved and du * du + dv * dv >= mv.re.after * mv.re.after then
		t.re_done = true
		local r = M7_Order(t.mover, mv.re.to[1], mv.re.to[2])
		SN_Rec("REORD", "k=" .. t.k .. "|name=" .. t.name .. "|mi=" .. t.mi .. "|move=" .. mv.name .. "|at=" ..
			M7_Fmt(u, v) .. "|to=" .. M7_Fmt(mv.re.to[1], mv.re.to[2]) .. "|r=" .. (F - t.fo) .. "|ret=" .. r)
		t.still = 0
	end
	if t.moved then
		if dl < 0.02 * 0.02 then
			t.still = t.still + 1
		else
			t.still = 0
		end
		if t.still >= cfg.still_polls and (mv.re == nil or t.re_done) then
			SN_Rec("STOP", "k=" .. t.k .. "|mi=" .. t.mi .. "|r=" .. (F - t.fo) .. "|at=" .. M7_Fmt(u, v))
			t.phase = "hold"
			t.fh = F
			return
		end
	end
	if F - t.fo >= cfg.leg_max then
		SN_Rec("LEGTO", "k=" .. t.k .. "|mi=" .. t.mi .. "|moved=" .. tostring(t.moved))
		t.phase = "hold"
		t.fh = F
	end
end

-- MvM: both orders in one service; then wait until both stood still or the time is up
function M7_MvmGo(t, F)
	local spec = t.spec
	local ra = M7_Order(t.ua, spec.a.to[1], spec.a.to[2])
	local rb = M7_Order(t.ub, spec.b.to[1], spec.b.to[2])
	t.fo = F
	t.phase = "mvm"
	t.sa, t.sb = 0, 0
	t.la = { M7_UV(t.ua) }
	t.lb = { M7_UV(t.ub) }
	SN_Rec("ORDER", "k=" .. t.k .. "|name=" .. t.name .. "|mi=1|move=mvm|a_to=" .. M7_Fmt(spec.a.to[1], spec.a.to[2]) ..
		"|b_to=" .. M7_Fmt(spec.b.to[1], spec.b.to[2]) .. "|ret=" .. ra .. "," .. rb)
	M7_Poll(t, F, true)
end

function M7_MvmTick(t, F)
	local cfg = SN_CFG.sq
	local function step(o, last, s)
		local u, v = M7_UV(o)
		if u == nil or last[1] == nil then
			return s, { u, v }
		end
		local d = (u - last[1]) * (u - last[1]) + (v - last[2]) * (v - last[2])
		if d < 0.02 * 0.02 then
			return s + 1, { u, v }
		end
		return 0, { u, v }
	end
	t.sa, t.la = step(t.ua, t.la, t.sa)
	t.sb, t.lb = step(t.ub, t.lb, t.sb)
	local done = F - t.fo > cfg.mvm_min and t.sa >= cfg.mvm_still and t.sb >= cfg.mvm_still
	if done or F - t.fo >= cfg.leg_max then
		SN_Rec("MVMEND", "k=" .. t.k .. "|r=" .. (F - t.fo) .. "|a=" .. M7_Fmt(M7_UV(t.ua)) .. "|b=" .. M7_Fmt(M7_UV(t.ub)) ..
			"|timeout=" .. tostring(not done))
		t.phase = "end"
	end
end

function M7_TrialTick(t, F)
	local cfg = SN_CFG.sq
	if t.phase == "ident" then
		if F >= t.fi then
			M7_Ident(t, F)
		end
		return
	end
	M7_Poll(t, F, false)
	if t.phase == "settle" then
		if F - t.fs >= cfg.settle then
			if t.kind == "mvm" then
				M7_MvmGo(t, F)
			else
				M7_NextMove(t, F)
			end
		end
		return
	end
	if t.phase == "move" then
		M7_MoveTick(t, F)
		return
	end
	if t.phase == "mvm" then
		M7_MvmTick(t, F)
		return
	end
	if t.phase == "hold" then
		if F - t.fh >= cfg.hold then
			M7_NextMove(t, F)
		end
	end
end

function M7_EndTrial(t, F)
	for _, x in ipairs(t.units or {}) do
		if x.role ~= "c" then
			SN_Despawn(x.o)
		end
	end
	for _, x in ipairs(t.units or {}) do
		if x.role == "c" then
			SN_Despawn(x.o)
		end
	end
	SN_Rec("TEND", "k=" .. t.k .. "|name=" .. t.name .. "|moves=" .. t.mi .. "|aborted=" .. tostring(t.aborted))
	SQ.cur = nil
	SQ.phase = "pause"
	SQ.next_f = F + SN_CFG.sq.gap
end

function SQ_Tick(F)
	local cfg = SN_CFG.sq
	if SQ.phase == "init" then
		if SQ.next_f == nil then
			SQ.next_f = cfg.start_frame
			SN_Say("TEXT_MOV2_T07_ARMED")
			SN_Rec("SCFG", "map=" .. SN_Str(cfg.map) .. "|site=" .. string.format("%.1f,%.1f,%.1f", cfg.site.x, cfg.site.y,
				cfg.site.z) .. "|theta0=" .. SN_Str(cfg.theta0) .. "|trials=" .. table.getn(cfg.trials) .. "|hold=" ..
				cfg.hold .. "|settle=" .. cfg.settle)
		end
		if F >= SQ.next_f then
			SQ.phase = "pause"
		end
		return
	end
	if SQ.phase == "pause" then
		if F >= SQ.next_f then
			SQ.ti = SQ.ti + 1
			local spec = cfg.trials[SQ.ti]
			if spec == nil then
				SN_Say("TEXT_MOV2_T07_DONE")
				SN_Finish("trials=" .. (SQ.ti - 1))
				return
			end
			SQ.phase = "trial"
			M7_Start(F, SQ.ti, spec)
		end
		return
	end
	local t = SQ.cur
	if t == nil then
		return
	end
	if t.phase ~= "end" then
		local ok, err = pcall(M7_TrialTick, t, F)
		if not ok then
			t.errs = t.errs + 1
			if t.errs <= 3 then
				SN_Rec("ERR", "where=trial|k=" .. t.k .. "|ph=" .. tostring(t.phase) .. "|err=" .. string.sub(SN_Str(err), 1, 200))
			end
			if t.errs >= 3 then
				t.phase = "end"
				t.aborted = "errors"
			end
		end
		if t.phase ~= "end" and F - t.f0 > cfg.trial_max then
			t.phase = "end"
			t.aborted = "timeout"
		end
	end
	if t.phase == "end" then
		M7_EndTrial(t, F)
	end
end
-- ==== end of mov2_t07_probe.lua (the arming and service drivers follow) ====
