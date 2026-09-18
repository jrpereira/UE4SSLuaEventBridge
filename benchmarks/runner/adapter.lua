-- Runs once on the game thread after F8. Override for samples with menus,
-- multiple local players or custom input ownership; no recurring scans.
return function()
    local controller = require("UEHelpers").GetPlayerController()
    assert(controller and controller:IsValid(), "No active player controller; enter gameplay first")
    local component
    local pawn = controller.Pawn
    if pawn and pawn:IsValid() then component = pawn.InputComponent end
    local class = StaticFindObject("/Script/EnhancedInput.EnhancedInputComponent")
    local function enhanced(obj)
        return obj and obj:IsValid() and obj:IsA(class)
    end
    if not enhanced(component) then component = controller.InputComponent end
    assert(enhanced(component), "No active EnhancedInputComponent; supply a sample adapter")
    local found = {}
    for _, obj in ipairs(FindAllOf("EnhancedInputLocalPlayerSubsystem") or {}) do
        local name = obj:GetFullName()
        if obj:IsValid() and not name:find("Default__", 1, true) then found[#found+1]=obj end
    end
    assert(#found == 1, "Expected one local-player subsystem; supply exact ownership in adapter")
    local function path(obj)
        return assert(obj:GetFullName():match("^%S+%s+(.+)$"), "Invalid object path")
    end
    -- Fresh sample components may not have an Unreal weak serial yet.
    -- Use the same reflected engine conversion used for generated actions;
    -- native OpenInput still verifies the resulting object identity.
    local system=StaticFindObject('/Script/Engine.Default__KismetSystemLibrary')
    assert(system and system:IsValid(), 'KismetSystemLibrary is unavailable')
    system:Conv_ObjectToSoftObjectReference(component)
    return {component_path=path(component), subsystem_path=path(found[1]), debug=false}
end
