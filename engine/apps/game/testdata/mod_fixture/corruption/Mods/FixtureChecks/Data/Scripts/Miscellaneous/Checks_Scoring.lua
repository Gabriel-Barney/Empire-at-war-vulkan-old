-- Sets a GlobalValue only when the session calls Base_Definitions (as a game scoring script does at startup): its main
-- chunk sets nothing.
function Base_Definitions()
    GlobalValue.Set("CHECKS_MOD_ID", "Checks")
end

-- mcx0: a call to a binding the port records without a consumer (binding ledger "recorded"); never run.
function Checks_Hide(obj)
    Hide_Sub_Object(obj, 1, "Checks_Part")
end
