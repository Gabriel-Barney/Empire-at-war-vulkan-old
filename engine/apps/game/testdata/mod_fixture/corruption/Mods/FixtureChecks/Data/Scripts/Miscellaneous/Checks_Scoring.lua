-- Sets a GlobalValue only when the session calls Base_Definitions (as a game scoring script does at startup): its main
-- chunk sets nothing.
function Base_Definitions()
    GlobalValue.Set("CHECKS_MOD_ID", "Checks")
end

-- mcx0: a call to a binding with an effect gap (binding ledger "partial": the port guessed part of it); never run.
function Checks_Compare(a, b)
    return StringCompare(a, b)
end
