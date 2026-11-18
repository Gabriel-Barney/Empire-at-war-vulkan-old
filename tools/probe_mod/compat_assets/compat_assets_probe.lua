-- ==== compat_assets: what retail draws for broken models, animations and particles (roadmap mcx2 t02) ====
-- tools/probe_mod/compat_assets/compat_assets_probe.lua. build_compat_assets_mod.py appends, to the shipped
-- BusyTacticalFreeStore.lua, a generated CA_CFG table and this file.
--
-- Question (docs/MOD_COMPAT_DESIGN.md sec. 10.2, decision D5): for each defect class, does retail draw the unit
-- (model, animation, particle), draw nothing, or stop? One slot = one variant type CAP_<KEY> whose model / idle
-- animation / particle system is a broken copy of a shipped one. Slots spawn one after another for the human player
-- on an open corridor far from both starts; the camera is pointed at each and held CA_CFG.hold frames, and a
-- Game_Message names the slot; the runner screenshots the window. Records tell which slot was on screen when, so a
-- crash is pinned to the slot that was spawning.
--
-- Arming: only the free store of the AI player whose faction is CA_CFG.ai_faction (REBEL) runs it, in Land mode,
-- and only when Find_Player(CA_CFG.human_faction) is human (so the main-menu background battle never arms it).
-- Output: Lua strings "CAQ|<KIND>|sid=<id>|n=<n>|f=<mode frame>|...|END" kept in CA_LOG, found by the runner's
-- read-only heap scan. Retail rules kept from earlier kits: Lua 5.0 (no '#', no '%', no math library); every engine
-- call on an object is gated by Is_Valid; Point_Camera_At with a position.

CA_LOG = {}
CA_N = 0
CA_SID = string.sub((string.gsub(tostring(CA_LOG), "[^%w]", "")), -6)
CA = { on = false, done = false, f = -1, tries = 0, pending = false, old_rate = nil, slot = 0, phase = "wait",
	t = 0, objs = nil, errs = 0 }
CA_API_BAD = {}

if CA_CFG == nil then
	CA_CFG = { session = "?", ai_faction = "?", human_faction = "?", slots = {} }
end

function CA_Str(v)
	return (string.gsub(tostring(v), "[^%w%._%-:,]", "_"))
end

function CA_Frame()
	local ok, t = pcall(GetCurrentTime)
	if ok and type(t) == "number" then
		return tonumber(string.format("%.0f", t * 30))
	end
	return nil
end

function CA_Rec(kind, fields)
	pcall(function()
		local f = CA_Frame()
		CA_N = CA_N + 1
		local body = fields or ""
		if string.len(body) > 440 then
			body = string.sub(body, 1, 440) .. "~"
		end
		CA_LOG[CA_N] = "CAQ|" .. kind .. "|sid=" .. CA_SID .. "|n=" .. CA_N .. "|f=" .. tostring(f or "?") .. "|" ..
			body .. "|END"
	end)
end

function CA_ApiFail(tag, err)
	if not CA_API_BAD[tag] then
		CA_API_BAD[tag] = true
		CA_Rec("APIERR", "api=" .. CA_Str(tag) .. "|err=" .. string.sub(CA_Str(err), 1, 150))
	end
end

function CA_Say(id)
	CA_Rec("SAY", "text=" .. id)
	pcall(Game_Message, id)
end

function CA_Valid(o)
	if o == nil then
		return false
	end
	local ok, v = pcall(function() return o.Is_Valid() end)
	return ok and v == true
end

function CA_Id(o)
	if not CA_Valid(o) then
		return "dead"
	end
	local id = nil
	pcall(function() id = o.Get_Object_ID() end)
	return CA_Str(id)
end

function CA_Pos(x, y, z)
	local ok, p = pcall(Create_Position, x, y, z or 0)
	if ok then
		return p
	end
	CA_ApiFail("Create_Position", p)
	return nil
end

function CA_Player(name)
	local p = nil
	pcall(function() p = Find_Player(name) end)
	return p
end

-- Spawn_Unit first (returns a table); a type it refuses (a Particle) through Create_Generic_Object.
function CA_Spawn(tname, pos, player)
	local ty = nil
	pcall(function() ty = Find_Object_Type(tname) end)
	if ty == nil then
		return {}, "no_type"
	end
	local ok, r = pcall(Spawn_Unit, ty, pos, player)
	if ok and type(r) == "table" and r[1] ~= nil then
		return r, "spawn_unit"
	end
	if ok and r ~= nil and type(r) ~= "table" and CA_Valid(r) then
		return { r }, "spawn_unit_single"
	end
	if not ok then
		CA_ApiFail("Spawn_Unit:" .. tname, r)
	end
	local ok2, g = pcall(Create_Generic_Object, ty, pos, player)
	if ok2 and g ~= nil and CA_Valid(g) then
		return { g }, "generic_object"
	end
	if not ok2 then
		CA_ApiFail("Create_Generic_Object:" .. tname, g)
	end
	return {}, "spawn_failed"
end

function CA_Describe(objs)
	local ids, alive, n = "", 0, 0
	for _, o in pairs(objs or {}) do
		n = n + 1
		if CA_Valid(o) then
			alive = alive + 1
		end
		if n <= 6 then
			ids = ids .. CA_Id(o) .. ","
		end
	end
	return "objs=" .. n .. "|alive=" .. alive .. "|ids=" .. ids
end

function CA_Camera(s)
	local p = CA_Pos(s.x, s.y, s.z)
	if p ~= nil then
		local ok, err = pcall(Point_Camera_At, p)
		if not ok then
			CA_ApiFail("Point_Camera_At", err)
		end
	end
end

-- ---- the slot driver ------------------------------------------------------------------------------------------

function CA_Step(F)
	local slots = CA_CFG.slots
	if CA.phase == "wait" then
		if F >= CA_CFG.start_frame then
			CA_Say("TEXT_CAP_ARMED")
			CA.phase = "spawn"
			CA.slot = 1
		end
		return
	end
	if CA.phase == "spawn" then
		local s = slots[CA.slot]
		if s == nil then
			CA.phase = "overview"
			local m = 1
			while slots[m + 1] ~= nil do
				m = m + 1
			end
			local c = slots[tonumber(string.format("%.0f", (m + 1) / 2))] or slots[1]
			CA_Camera(c)
			CA_Say("TEXT_CAP_OVERVIEW")
			CA_Rec("OVERVIEW", "slots=" .. m)
			CA.t = F
			return
		end
		local human = CA_Player(CA_CFG.human_faction)
		CA_Rec("SPAWN_BEGIN", "slot=" .. CA.slot .. "|key=" .. s.key .. "|type=" .. s.type)
		CA_Camera(s)
		local objs, how = CA_Spawn(s.type, CA_Pos(s.x, s.y, s.z), human)
		CA.objs = objs
		for _, o in pairs(objs) do
			if CA_Valid(o) then
				pcall(function() o.Make_Invulnerable(true) end)
				pcall(function() o.Prevent_AI_Usage(true) end)
			end
		end
		CA_Rec("SPAWN", "slot=" .. CA.slot .. "|key=" .. s.key .. "|how=" .. how .. "|" .. CA_Describe(objs))
		CA_Say("TEXT_CAP_SLOT_" .. s.key)
		CA_Rec("SHOW", "slot=" .. CA.slot .. "|key=" .. s.key .. "|x=" .. s.x .. "|y=" .. s.y)
		CA.phase = "hold"
		CA.t = F
		return
	end
	if CA.phase == "hold" then
		local s = slots[CA.slot]
		if F - CA.t == 60 then
			CA_Camera(s)  -- again: the first point may come before the camera finished the previous pan
		end
		if F - CA.t >= CA_CFG.hold then
			CA_Rec("HOLD_END", "slot=" .. CA.slot .. "|key=" .. s.key .. "|" .. CA_Describe(CA.objs))
			CA.phase = "gap"
			CA.t = F
		end
		return
	end
	if CA.phase == "gap" then
		if F - CA.t >= CA_CFG.gap then
			CA.slot = CA.slot + 1
			CA.phase = "spawn"
		end
		return
	end
	if CA.phase == "overview" then
		if F - CA.t >= CA_CFG.overview then
			CA_Say("TEXT_CAP_DONE")
			CA_Finish("why=all_slots")
		end
	end
end

-- ---- arming and the service drivers ---------------------------------------------------------------------------

function CA_Arm()
	local fac, mode, human = "?", "?", true
	pcall(function() fac = string.upper(tostring(PlayerObject.Get_Faction_Name())) end)
	pcall(function() mode = tostring(Get_Game_Mode()) end)
	pcall(function() human = PlayerObject.Is_Human() end)
	if (fac == "?" or fac == "NIL") and CA.tries < 200 then
		CA.pending = true
		return
	end
	CA.pending = false
	local human_ok = false
	pcall(function() human_ok = Find_Player(CA_CFG.human_faction).Is_Human() == true end)
	if mode == "Land" and not human_ok and CA.tries < 60 then
		CA.pending = true
		return
	end
	if mode == "Land" and fac == CA_CFG.ai_faction and human ~= true and human_ok and not CA_ARMED_ONCE then
		CA_ARMED_ONCE = true
		CA.on = true
		CA.old_rate = ServiceRate
		ServiceRate = 0.05
		CA_Rec("ARM", "fac=" .. CA_Str(fac) .. "|mode=" .. CA_Str(mode) .. "|session=" .. CA_Str(CA_CFG.session))
	else
		CA_Rec("SKIP", "fac=" .. CA_Str(fac) .. "|mode=" .. CA_Str(mode) .. "|human_side=" .. tostring(human_ok))
	end
end

function CA_Finish(extra)
	if CA.done then
		return
	end
	CA.done = true
	if CA.old_rate ~= nil then
		ServiceRate = CA.old_rate
	end
	local bad = ""
	for k, _ in pairs(CA_API_BAD) do
		bad = bad .. k .. ","
	end
	CA_Rec("DONE", "records=" .. CA_N .. "|apierr=" .. string.sub(CA_Str(bad), 1, 220) .. "|" .. (extra or ""))
end

function CA_Tick(src)
	if CA.pending then
		CA.tries = CA.tries + 1
		pcall(CA_Arm)
	end
	if not CA.on or CA.done then
		return
	end
	local F = CA_Frame()
	if F == nil or F == CA.f then
		return
	end
	CA.f = F
	local ok, err = pcall(CA_Step, F)
	if not ok then
		CA.errs = CA.errs + 1
		CA_Rec("ERR", "slot=" .. CA.slot .. "|phase=" .. CA.phase .. "|err=" .. string.sub(CA_Str(err), 1, 200))
		if CA.errs >= 50 then
			CA_Finish("why=errors")
		end
	end
end

function CA_Quiet()
	return CA.on and not CA.done
end

CA_Orig_Base_Definitions = Base_Definitions
function Base_Definitions()
	local r = nil
	if CA_Orig_Base_Definitions then
		r = CA_Orig_Base_Definitions()
	end
	CA.pending = true
	pcall(CA_Arm)
	return r
end

CA_Orig_FreeStoreService = FreeStoreService
function FreeStoreService()
	pcall(CA_Tick, "fss")
	if CA_Quiet() or CA_Orig_FreeStoreService == nil then
		return
	end
	return CA_Orig_FreeStoreService()
end

CA_Orig_On_Unit_Service = On_Unit_Service
function On_Unit_Service(object)
	pcall(CA_Tick, "ous")
	if CA_Quiet() or CA_Orig_On_Unit_Service == nil then
		return
	end
	return CA_Orig_On_Unit_Service(object)
end

CA_Rec("LOAD", "session=" .. CA_Str(CA_CFG.session) .. "|slots=" .. CA_Str(CA_CFG.human_faction))
-- ==== end of compat_assets_probe.lua ====
