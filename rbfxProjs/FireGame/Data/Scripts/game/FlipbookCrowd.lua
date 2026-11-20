-- Resource/name adapter. C++ owns animation, node following and rendering batches.
local Config = require("game.Config")
local FlipbookCrowd = {}
FlipbookCrowd.__index = FlipbookCrowd

function FlipbookCrowd.Build(scene)
    local batch = scene:GetComponent("FrameSpriteBatch") or scene:CreateComponent("FrameSpriteBatch")
    batch:SetAutoUpdate(false)
    batch:SetStatisticsEnabled(Config.PERF_TELEMETRY == true)
    return setmetatable({scene=scene, batch=batch, skins={},
        technique=GetResource("Technique", Config.FLIPBOOK_TECHNIQUE)}, FlipbookCrowd)
end

local function endsWith(str, suffix)
    return str:sub(-#suffix) == suffix
end

function FlipbookCrowd:_GetSkin(skin)
    if self.skins[skin] then return self.skins[skin] end
    local st = {active={}, clipByStack={}}
    self.skins[skin] = st
    local baker = FlipbookBaker()
    local path = Config.PirateFlipbook(skin)
    if not baker:Load(path) or not self.technique then
        LogError("FlipbookCrowd: cannot load " .. path .. ": " .. tostring(baker:GetLastError()))
        st.failed = true
        return st
    end
    st.baker = baker
    st.animations = FrameSpriteAnimator()
    for _, stack in ipairs(Config.PIRATE_CLIP_STACKS) do
        for c = 0, baker:GetNumClips() - 1 do
            if endsWith(baker:GetClipName(c), stack) then
                if st.animations:AddAtlasAnimation(stack, baker, c, stack == "Run") then
                    st.clipByStack[stack] = c
                end
                break
            end
        end
    end
    st.material = Material()
    st.material:SetTechnique(0, self.technique)
    st.material:SetTexture("Albedo", baker:GetTexture())
    LogInfo("FrameSprite: loaded " .. skin .. " clips=" .. baker:GetNumClips() .. " fps=" .. baker:GetFps())
    return st
end

-- One native batch update after gameplay movement preserves hit-stop/worldDt.
function FlipbookCrowd:Update(dt)
    self.batch:Update(dt)
end

function FlipbookCrowd:Acquire(skin, node)
    local st = self:_GetSkin(skin)
    if st.failed then return nil end
    local sprite = node:CreateComponent("FrameSprite")
    sprite:SetMaterial(st.material)
    sprite:SetSize(Vector2(Config.PIRATE_CARD_SIZE, Config.PIRATE_CARD_SIZE))
    sprite:SetOffset(Vector3(0, Config.PIRATE_CARD_CENTER, 0))
    local animator = FrameSpriteAnimator()
    animator:CopyAnimations(st.animations)
    animator:SetSprite(sprite)
    local handle = {st=st, sprite=sprite, animator=animator}
    st.active[handle] = true
    return handle
end

function FlipbookCrowd:Play(handle, stack)
    if not handle or not handle.st then return end
    if not handle.st.clipByStack[stack] then stack = "Run" end
    handle.animator:PlayAni(stack)
end

function FlipbookCrowd:Release(handle)
    if not handle or not handle.st then return end
    handle.st.active[handle] = nil
    handle.animator:SetSprite(nil)
    handle.sprite:Remove()
    handle.st, handle.sprite, handle.animator = nil, nil, nil
end

function FlipbookCrowd:Reset()
    for _, st in pairs(self.skins) do
        for handle in pairs(st.active) do self:Release(handle) end
    end
    self.batch:Update(0)
end

return FlipbookCrowd
