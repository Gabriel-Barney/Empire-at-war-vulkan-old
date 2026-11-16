-- ==== gai3 t04 / t07: goal and target keys for the Underworld build plans (channel P, --uw-infra builds only) ====
-- Appended after a2fb_plan_hook.lua (build_a2f_mod.UW_GOAL_EXTRA) to the unchanged shipped scripts of
-- build_a2f_mod.UW_GOAL_PLANS: BuildGroundForcesPlan, BuildSpaceForcesPlan, the four Underworld structure plans and
-- UpgradeStarbasePlan. Adds to PSTART and to the end records (PRET / PEXIT / PFAIL):
--   goal=  @@FORCE@@.Get_Goal_Type_Name() (the goal the plan serves: one plan script can serve several goals, e.g.
--          Build_Initial_Groundbase_Only | Build_Underworld_Barracks)
--   tgt=   the plan's Target, named through its game object (Get_Game_Object, stagek=obj in a2fb_saboteur_plan.lua),
--          else Get_Type directly; tgtk= how it was named (obj / type / nil / ERR)
-- The same reads the DeploySaboteur extension makes in retail (run 20261108-212556 answered all of them). Every read
-- is in pcall; A2FB_PlanX puts the whole function in another pcall, so a failure costs these keys only.
-- What the PSTART tells (A2M_R3_RE_A.md section 2): unalloc= = the net cost of the plan's selection; the PS sample
-- after it = the faction's Offensive / Infrastructure pools at the start (A2FB BFB decode).

function A2FB_GoalTgt(o)
	if o == nil then
		return " tgt=nil tgtk=nil"
	end
	local okg, g = pcall(function() return o.Get_Game_Object() end)
	if okg and g ~= nil then
		local okn, n = pcall(function() return g.Get_Type().Get_Name() end)
		if okn and n ~= nil then
			return " tgt=" .. string.upper(tostring(n)) .. " tgtk=obj"
		end
	end
	local okt, n2 = pcall(function() return o.Get_Type().Get_Name() end)
	if okt and n2 ~= nil then
		return " tgt=" .. string.upper(tostring(n2)) .. " tgtk=type"
	end
	return " tgt=ERR tgtk=ERR"
end

function A2FB_GoalKeys()
	local goal = A2FB_Str(function() return @@FORCE@@.Get_Goal_Type_Name() end)
	local tgt = nil
	pcall(function() tgt = Target end)
	return " goal=" .. goal .. A2FB_GoalTgt(tgt)
end

function A2FB_PlanStartX()
	return A2FB_GoalKeys()
end

function A2FB_PlanEndX(kind)
	return A2FB_GoalKeys()
end
