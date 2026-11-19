local Config = require("game.Config")
local Quality = {}

function Quality.Apply(scene)
    local cfg = Config.MOBILE_QUALITY
    local sdk = GetSubsystem("MinigameSDK")
    if not cfg.enabled or not (cfg.preview or (sdk and sdk:IsAvailable())) then
        return
    end
    local pipeline = scene:GetComponent("RenderPipeline")
    if not pipeline then
        print("FireGame quality: scene has no RenderPipeline")
        return
    end
    pipeline:SetAttribute("PCF Kernel Size", cfg.pcfKernel)
    if cfg.disableShadows then
        pipeline:SetAttribute("Enable Shadows", false)
    end
    if cfg.useLDR then
        -- RenderPipelineColorSpace::LinearLDR; Lua enum attributes take integers.
        pipeline:SetAttribute("Color Space", 1)
    end
    if cfg.disablePostprocess then
        -- Empty postprocess list still renders the scene through the main pipeline.
        pipeline:SetAttribute("Render Path", {
            type = "RenderPath", name = "RenderPaths/NoPostprocess.renderpath",
        })
    elseif cfg.minimalPostprocess then
        pipeline:SetAttribute("Render Path", {
            type = "RenderPath", name = "RenderPaths/Mobile.renderpath",
        })
    end
    print(string.format("FireGame quality: PCF=%d LDR=%s minimalPostprocess=%s noPostprocess=%s noShadows=%s",
        cfg.pcfKernel, tostring(cfg.useLDR), tostring(cfg.minimalPostprocess),
        tostring(cfg.disablePostprocess), tostring(cfg.disableShadows)))
end

return Quality
