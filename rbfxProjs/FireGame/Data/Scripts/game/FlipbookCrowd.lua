-- Scripts/game/FlipbookCrowd.lua
--
-- 海盗序列帧（flipbook）群体渲染器：把小海盗从 3D 蒙皮 AnimatedModel 降级成相机朝向的
-- 2D 序列帧卡片（BillboardSet 合批），对齐 FireGame "草=小海盗" 的混烧定案。
--
-- 为什么这样切：
--   * 每个海盗仍保留一个 Scene node 承载全部玩法数学（NearestTarget / 子弹扫掠 / 溅射 /
--     移除倒计时都以 u.node:GetWorldPosition() 为准，见 Shooting.lua 与 Enemy.lua）。这里
--     只管"画"，把 node 世界位置同步到 billboard，逐帧改写 billboard.uv 切图集格。
--   * BillboardSet 一集合批一次 draw call；但一个 set 只能挂一张纹理，故"每皮一个 set"
--     （与既往分组合批决策一致）：同种海盗共用一个 BillboardSet，种间各一次 dc。
--   * 序列帧只烘了单朝向（rotations=1），billboard 默认永远朝相机，node 的世界旋转对视觉
--     无影响（玩法无需转身观感）。
--
-- 生命周期：Enemy 在 SpawnPirate 里 Acquire 一个句柄、状态切换时 Play(stack)、移动后由本类
-- Update 同步位置/UV、移除时 Release 归还槽位。槽位用 free-list 复用，SetNumBillboards 只增
-- 不减（resize 扩容保留已有 billboard，见引擎 BillboardSet.cpp）。

local Config = require("game.Config")

---@class FlipbookCrowd 海盗序列帧 billboard 群体管理器
local FlipbookCrowd = {}
FlipbookCrowd.__index = FlipbookCrowd

--- 每皮惰性加载后的状态：atlas + 材质 + BillboardSet + clip 索引表 + 槽位池。
---@param scene Scene
---@return FlipbookCrowd
function FlipbookCrowd.Build(scene)
    local self = setmetatable({}, FlipbookCrowd)
    self.root = scene:CreateChild("FlipbookCrowd")
    self.technique = GetResource("Technique", Config.FLIPBOOK_TECHNIQUE)
    ---@type table<string, table> skin 资产名 -> 皮肤状态
    self.skins = {}
    return self
end

-- 取 clip 名的尾缀是否等于某动画栈名（clip 名形如 Characters_Anne_CharacterArmatureRun）。
local function endsWith(str, suffix)
    return str:sub(-#suffix) == suffix
end

--- 惰性解析某皮肤：Load 图集、建材质与 BillboardSet、把 Run/Sword/Death 映射到 clip 索引。
--- 失败（资产未烘焙）记 failed 并返回，调用方据此跳过绘制（海盗仍在、只是不可见，不崩）。
---@param skin string
---@return table
function FlipbookCrowd:_GetSkin(skin)
    local st = self.skins[skin]
    if st then
        return st
    end

    st = {
        skin = skin,
        failed = false,
        capacity = 0,        -- 已分配的 billboard 槽位数（只增）
        free = {},           -- 空闲槽位下标栈
        active = {},         -- handle -> true
        clipByStack = {},    -- "Run"/"Sword"/"Death" -> clip 索引
        frames = {},         -- clip 索引 -> 帧数
        loopTime = {},       -- clip 索引 -> 循环时长（秒）
    }
    self.skins[skin] = st

    local path = Config.PirateFlipbook(skin)
    local baker = FlipbookBaker()
    if not baker:Load(path) then
        LogError("FlipbookCrowd: 加载序列帧图集失败 '" .. path .. "': " .. tostring(baker:GetLastError())
            .. "（先用编辑器右键该 fbx -> Bake Animation Flipbook 烘 Run/Sword/Death）")
        st.failed = true
        return st
    end
    if not self.technique then
        LogError("FlipbookCrowd: 找不到技法 '" .. Config.FLIPBOOK_TECHNIQUE .. "'")
        st.failed = true
        return st
    end

    st.baker = baker
    st.fps = baker:GetFps()
    local numClips = baker:GetNumClips()
    for _, stack in ipairs(Config.PIRATE_CLIP_STACKS) do
        for c = 0, numClips - 1 do
            if endsWith(baker:GetClipName(c), stack) then
                st.clipByStack[stack] = c
                local fc = baker:GetClipFrameCount(c)
                st.frames[c] = fc
                st.loopTime[c] = fc / st.fps
                break
            end
        end
    end

    -- 单纹理一材质：Unlit + 顶点色 + alpha 混合；rbfx Unlit/DIFFMAP 采的是 "Albedo" 单元。
    local material = Material()
    material:SetTechnique(0, self.technique)
    material:SetTexture("Albedo", baker:GetTexture())

    st.material = material
    st.bset = self.root:CreateComponent("BillboardSet")
    st.bset:SetMaterial(material)
    st.bset:SetNumBillboards(0)
    LogInfo("FlipbookCrowd: 皮肤 '" .. skin .. "' 就绪：atlas " .. baker:GetAtlasWidth() .. "x" .. baker:GetAtlasHeight()
        .. "，" .. numClips .. " clips，fps=" .. st.fps
        .. "，映射 " .. tostring(st.clipByStack["Run"]) .. "/" .. tostring(st.clipByStack["Sword"]) .. "/" .. tostring(st.clipByStack["Death"]))
    return st
end

--- 领一个 billboard 槽位（优先复用空闲）。返回句柄，或皮肤不可用时返回 nil。
---@param skin string
---@param node Node 海盗的逻辑节点（世界位置来源）
function FlipbookCrowd:Acquire(skin, node)
    local st = self:_GetSkin(skin)
    if st.failed then
        return nil
    end

    local idx
    if #st.free > 0 then
        idx = table.remove(st.free)
    else
        idx = st.capacity
        st.capacity = st.capacity + 1
        st.bset:SetNumBillboards(st.capacity)   -- resize 扩容，保留既有 billboard
        local b = st.bset:GetBillboard(idx)
        b.size = Vector2(Config.PIRATE_CARD_SIZE, Config.PIRATE_CARD_SIZE)
        b.color = Color(1.0, 1.0, 1.0)
    end

    local handle = { st = st, idx = idx, node = node, clip = nil, t = 0.0, oneShot = false }
    st.active[handle] = true
    return handle
end

--- 切到某动画栈并从首帧重播。stack ∈ {"Run","Sword","Death"}；Run 循环，其余单播定格末帧。
---@param handle table|nil
---@param stack string
function FlipbookCrowd:Play(handle, stack)
    if not handle then
        return
    end
    local st = handle.st
    local c = st.clipByStack[stack]
    if c == nil then
        c = st.clipByStack["Run"]   -- 兜底：缺该栈就用 Run
    end
    if c == nil then
        handle.clip = nil
        return
    end
    handle.clip = c
    handle.t = 0.0
    handle.oneShot = (stack ~= "Run")
end

--- 每帧：对所有在用句柄推进时间、按 node 世界位置摆卡片、改写 UV 切帧，然后各 set 提交一次。
---@param dt number
function FlipbookCrowd:Update(dt)
    for _, st in pairs(self.skins) do
        if not st.failed then
            local dirty = false
            for h in pairs(st.active) do
                local c = h.clip
                if c and h.node then
                    h.t = h.t + dt
                    local fc = st.frames[c]
                    local frame
                    if h.oneShot then
                        frame = math.floor(h.t * st.fps)
                        if frame > fc - 1 then
                            frame = fc - 1
                        end
                    else
                        local loop = st.loopTime[c]
                        local tt = loop > 0.0 and (h.t % loop) or 0.0
                        frame = math.floor(tt * st.fps)
                        if frame > fc - 1 then
                            frame = fc - 1
                        end
                    end
                    local wp = h.node:GetWorldPosition()
                    local b = st.bset:GetBillboard(h.idx)
                    b.position = Vector3(wp.x, wp.y + Config.PIRATE_CARD_CENTER, wp.z)
                    b.uv = st.baker:GetClipCellUV(c, frame, 0)
                    b.enabled = true
                    dirty = true
                end
            end
            if dirty then
                st.bset:Commit()
            end
        end
    end
end

--- 归还槽位（海盗移除时）：停用 billboard 并入空闲栈。
---@param handle table|nil
function FlipbookCrowd:Release(handle)
    if not handle then
        return
    end
    local st = handle.st
    if st and not st.failed and st.active[handle] then
        local b = st.bset:GetBillboard(handle.idx)
        b.enabled = false
        st.bset:Commit()
        st.active[handle] = nil
        st.free[#st.free + 1] = handle.idx
    end
    handle.st = nil
end

--- 清空全部在用句柄（换局）：保留已加载的图集/材质/set 供复用，只把槽位归零。
function FlipbookCrowd:Reset()
    for _, st in pairs(self.skins) do
        if not st.failed then
            st.bset:SetNumBillboards(0)
        end
        st.capacity = 0
        st.free = {}
        st.active = {}
    end
end

return FlipbookCrowd
