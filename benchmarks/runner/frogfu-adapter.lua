-- Frog Fu diagnostic setup only: route keyboard input to the game viewport.
return function()
    local controller=require('UEHelpers').GetPlayerController()
    assert(controller and controller:IsValid(), 'No local player controller')
    local widgets=StaticFindObject('/Script/UMG.Default__WidgetBlueprintLibrary')
    assert(widgets and widgets:IsValid(), 'WidgetBlueprintLibrary unavailable')
    widgets:SetInputMode_GameOnly(controller, false)
    widgets:SetFocusToGameViewport()
    return assert(loadfile('C:/TestInput/base-adapter.lua'))()()
end
