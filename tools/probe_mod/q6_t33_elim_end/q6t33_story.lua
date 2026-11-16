-- ==== Q6 t33 / t35 probe: what holds the galactic end screen after an elimination (tools/probe_mod/q6_t33_elim_end) ====
-- Galactic plot script of the campaigns "Q6T33: <variant>" (human Empire, nothing owned at start), installed by
-- build_q6t33_mod.py as Data\Scripts\Story\Q6T33_Story_<variant>.lua with the @@...@@ fields replaced:
--   MODE D  at t+20 s the script gives the neutral Abregado Rae to the Rebel AI (the human owns nothing): the Rebel's
--           implicit Opponent_Controls_No_Planets passes (q6 t25 run: ended within 1 s)
--   MODE E  the script gives Tatooine to the human at t+10 s and makes it Neutral at t+40 s (q6 t25 run: ended
--           4.3-5.3 s later); the campaign XML decides where the human's Star_Destroyer starts (Tatooine, Byss, none)
--   MSG     text id shown with Game_Message right after the D / E event, or "" for none
-- The same as tools/probe_mod/q6_t15_probes/q6_story_elim.lua modes D / E (same galaxy, same times), plus:
--   heartbeats every 0.25 s from 3 s before the event to 25 s after it (1 s otherwise), each with the game mode, the
--   human's Star_Destroyer planet and the owners; the gate poller of q6t33_run.py reads the end-screen gates from
--   memory every few ms, so these records only anchor the Lua side.
-- Output channel: strings "Q6T|<KIND>|var=..|sid=..|n=..|f=..|t=..|...|END" in Q6T_LOG (heap scan).
require("PGStoryMode")

Q6T_VARIANT = "@@VARIANT@@"
Q6T_MODE = "@@MODE@@"
Q6T_MSG = "@@MSG@@"
Q6T_PLANETS = { "Tatooine", "Byss", "Abregado_Rae", "Jabiim" }
Q6T_D_T = 20
Q6T_E_GIVE_T = 10
Q6T_E_T = 40
Q6T_FAST_BEFORE = 3
Q6T_FAST_AFTER = 25
Q6T_FAST_STEP = 0.25
Q6T_END_T = 120

Q6T_LOG = {}
Q6T_N = 0
Q6T_SID = string.sub((string.gsub(tostring(Q6T_LOG), "[^%w]", "")), -6)
Q6T_Started = false

function Q6T_Time()
	local ok, t = pcall(GetCurrentTime)
	if ok and type(t) == "number" then
		return t
	end
	return nil
end

function Q6T_Rec(kind, fields)
	pcall(function()
		Q6T_N = Q6T_N + 1
		local t = Q6T_Time()
		local fs, ts = "?", "?"
		if t then
			fs = string.format("%.0f", t * 30)
			ts = string.format("%.3f", t)
		end
		local s = "Q6T|" .. kind .. "|var=" .. Q6T_VARIANT .. "|sid=" .. Q6T_SID .. "|n=" .. Q6T_N .. "|f=" .. fs ..
			"|t=" .. ts
		if fields and fields ~= "" then
			s = s .. "|" .. fields
		end
		Q6T_LOG[Q6T_N] = s .. "|END"
	end)
end

function Q6T_Str(v)
	return (string.gsub(tostring(v), "[^%w%._%-:]", "_"))
end

function Q6T_Say(id)
	if id == nil or id == "" then
		Q6T_Rec("SAY", "text=none")
		return
	end
	Q6T_Rec("SAY", "text=" .. id)
	pcall(Game_Message, id)
end

function Q6T_Faction(p)
	if p == nil then
		return "nil"
	end
	local ok, n = pcall(function() return p.Get_Faction_Name() end)
	if ok and n ~= nil then
		return Q6T_Str(n)
	end
	return "?"
end

function Q6T_Owners(empire)
	local s = ""
	local owned = 0
	local me = string.upper(Q6T_Faction(empire))
	for _, n in ipairs(Q6T_PLANETS) do
		local f = "?"
		pcall(function() f = Q6T_Faction(FindPlanet(n).Get_Owner()) end)
		s = s .. "|" .. string.sub(n, 1, 3) .. "=" .. string.sub(f, 1, 4)
		if string.upper(f) == me then  -- compare by faction name: two wrappers of one player need not be ==
			owned = owned + 1
		end
	end
	return "owned=" .. owned .. s
end

-- Game mode, and where the human's Star_Destroyer is: "gm=Galactic|sd=Tatooine|sdp=Fleet" (sd=none without one).
function Q6T_Where()
	local gm = "?"
	pcall(function() gm = Q6T_Str(Get_Game_Mode()) end)
	local sd, sdp = "none", "?"
	pcall(function()
		local u = Find_First_Object("Star_Destroyer")
		if u then
			sd = "?"
			pcall(function() sd = Q6T_Str(u.Get_Planet_Location().Get_Type().Get_Name()) end)
			pcall(function() sdp = Q6T_Str(u.Get_Parent_Object().Get_Type().Get_Name()) end)
		end
	end)
	return "gm=" .. gm .. "|sd=" .. sd .. "|sdp=" .. string.sub(sdp, 1, 24)
end

function Definitions()
	StoryModeEvents = { Q6T_Begin = Q6T_State_Begin }
end

function Q6T_State_Begin(message)
	if message == OnEnter and not Q6T_Started then
		Q6T_Started = true
		Create_Thread("Q6T_Run")
	end
end

function Q6T_Change(planet, player, kind)
	local ok, err = pcall(function() FindPlanet(planet).Change_Owner(player) end)
	return "ok=" .. tostring(ok) .. "|err=" .. string.sub(Q6T_Str(err), 1, 40)
end

function Q6T_Run()
	local empire = Find_Player("Empire")
	local neutral = Find_Player("Neutral")
	local rebel = Find_Player("Rebel")
	local human = "?"
	pcall(function() human = tostring(empire.Is_Human()) end)
	Q6T_Rec("START", "mode=" .. Q6T_MODE .. "|msg=" .. Q6T_MSG .. "|human=" .. human .. "|" .. Q6T_Owners(empire) .. "|" ..
		Q6T_Where())
	Q6T_Say("TEXT_Q6T33_RUNNING_" .. Q6T_MODE)
	local t0 = Q6T_Time() or 0
	local ev_t = Q6T_E_T
	if Q6T_MODE == "D" then
		ev_t = Q6T_D_T
	end
	local next_hb = t0
	local given, done = false, false
	while true do
		local now = Q6T_Time() or 0
		local dt = now - t0
		if Q6T_MODE == "E" and not given and dt >= Q6T_E_GIVE_T then
			given = true
			local r = Q6T_Change("Tatooine", empire)
			Q6T_Rec("GIVE", r .. "|" .. Q6T_Owners(empire) .. "|" .. Q6T_Where())
			Q6T_Say("TEXT_Q6T33_E_GIVEN")
		end
		if not done and dt >= ev_t then
			done = true
			local kind, r = "TAKE", nil
			if Q6T_MODE == "D" then
				kind = "AIGIVE"
				r = Q6T_Change("Abregado_Rae", rebel)
			else
				r = Q6T_Change("Tatooine", neutral)
			end
			Q6T_Rec(kind, r .. "|" .. Q6T_Owners(empire) .. "|" .. Q6T_Where())
			Q6T_Say(Q6T_MSG)
			next_hb = (Q6T_Time() or 0)
		end
		if (Q6T_Time() or 0) >= next_hb then
			local fast = dt >= ev_t - Q6T_FAST_BEFORE and dt < ev_t + Q6T_FAST_AFTER
			if fast then
				next_hb = next_hb + Q6T_FAST_STEP
			else
				next_hb = next_hb + 1
			end
			Q6T_Rec("HB", "dt=" .. string.format("%.2f", dt) .. "|" .. Q6T_Owners(empire) .. "|" .. Q6T_Where())
		end
		if dt >= Q6T_END_T then
			break
		end
		if dt >= ev_t - Q6T_FAST_BEFORE - 1 and dt < ev_t + Q6T_FAST_AFTER then
			Sleep(0.05)
		else
			Sleep(0.5)
		end
	end
	Q6T_Rec("DONE", "records=" .. Q6T_N .. "|" .. Q6T_Owners(empire))
	Q6T_Say("TEXT_Q6T33_DONE")
end

Q6T_Rec("TOP", "")
-- ==== end of the Q6 t33 probe ====
