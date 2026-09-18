-- One-time world rendering disable; retain a real viewport for SendInput.
return function()
    local controller = require('UEHelpers').GetPlayerController()
    local statics = StaticFindObject('/Script/Engine.Default__GameplayStatics')
    assert(controller:IsValid() and statics:IsValid(), 'World rendering context unavailable')
    statics:SetEnableWorldRendering(controller, false)
    local enabled = statics:GetEnableWorldRendering(controller)
    local f = assert(io.open('C:/TestResults/world-rendering.txt', 'w'))
    f:write('GetEnableWorldRendering=' .. tostring(enabled) .. '\n')
    f:close()
    assert(enabled == false, 'World rendering remained enabled')
    local setup = (function()
-- Diagnostic rendering reduction; real viewport and real keyboard input remain.
return function()
    local controller=require('UEHelpers').GetPlayerController()
    local system=StaticFindObject('/Script/Engine.Default__KismetSystemLibrary')
    local widgets=StaticFindObject('/Script/UMG.Default__WidgetBlueprintLibrary')
    widgets:SetInputMode_GameOnly(controller,false)
    widgets:SetFocusToGameViewport()
    local f=assert(io.open('C:/TestResults/render-settings.txt','w'))
    for _,command in ipairs({'r.SetRes 160x90w','r.ScreenPercentage 10','sg.ShadowQuality 0',
        'sg.PostProcessQuality 0','sg.EffectsQuality 0','sg.ViewDistanceQuality 0','r.VSync 0','t.MaxFPS 0'}) do
        system:ExecuteConsoleCommand(controller,command,controller)
        f:write(command..'\n')
    end
    f:close()
    return assert(loadfile('C:/TestInput/base-adapter.lua'))()()
end

    end)()
    return setup()
end
