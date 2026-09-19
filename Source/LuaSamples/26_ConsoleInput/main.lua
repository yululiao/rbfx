-- LuaSamples/26_ConsoleInput/main.lua
-- Lua port of Source/Samples/26_ConsoleInput: a tiny text adventure played
-- through the engine console. Demonstrates Console command subscription and
-- stdin-style input handling (the OS console window part of the C++ sample is
-- skipped here; use the in-engine console instead).

local hungerLevels = {
    "bursting",
    "well-fed",
    "fed",
    "hungry",
    "very hungry",
    "starving"
}

local urhoThreatLevels = {
    "Suddenly Urho appears from a dark corner of the fish tank",
    "Urho seems to have his eyes set on you",
    "Urho is homing in on you mercilessly"
}

local app = Sample:new()
app.gameOn = false
app.foodAvailable = false
app.eatenLastTurn = false
app.numTurns = 0
app.hunger = 2
app.urhoThreat = 0

-- Logging appears both in the engine console and stdout (print() is redirected
-- into the engine log by the VM, matching every other sample's logging path).
local function Print(output)
    print(output)
end

function app:OnStart()
    local input = GetSubsystem("Input")
    input:SetMouseMode(MM.FREE)
    input:SetMouseVisible(true)

    -- Subscribe to console commands
    SubscribeToEvent("ConsoleCommand", function(data)
        if data.Id == "ConsoleInput" then
            self:HandleInput(data.Command)
        end
    end)

    -- Enable filesystem interaction in console
    -- (FileSystem console command execution flag is engine-side default on)

    -- Show the console by default; it will show the text edit field when at
    -- least one ConsoleCommand subscriber exists
    local console = GetSubsystem("Console")
    console:SetCommandInterpreter("ConsoleInput")
    console:SetVisible(true)

    self:StartGame()
end

function app:StartGame()
    Print("Welcome to the Urho adventure game! You are the newest fish in the tank; your\n"
        .. "objective is to survive as long as possible. Beware of hunger and the merciless\n"
        .. "predator cichlid Urho, who appears from time to time. Evading Urho is easier\n"
        .. "with an empty stomach. Type 'help' for available commands.")

    self.gameOn = true
    self.foodAvailable = false
    self.eatenLastTurn = false
    self.numTurns = 0
    self.hunger = 2
    self.urhoThreat = 0
end

function app:EndGame(message)
    Print(message)
    Print("Game over! You survived " .. self.numTurns .. " turns.\n"
        .. "Do you want to play again (Y/N)?")

    self.gameOn = false
end

function app:Advance()
    if self.urhoThreat > 0 then
        self.urhoThreat = self.urhoThreat + 1
        if self.urhoThreat > 3 then
            self:EndGame("Urho has eaten you!")
            return
        end
    elseif self.urhoThreat < 0 then
        self.urhoThreat = self.urhoThreat + 1
    end
    if self.urhoThreat == 0 and Random() < 0.2 then
        self.urhoThreat = self.urhoThreat + 1
    end

    if self.urhoThreat > 0 then
        Print(urhoThreatLevels[self.urhoThreat] .. ".")
    end

    if (self.numTurns % 4) == 0 and not self.eatenLastTurn then
        self.hunger = self.hunger + 1
        if self.hunger > 5 then
            self:EndGame("You have died from starvation!")
            return
        else
            Print("You are " .. hungerLevels[self.hunger + 1] .. ".")
        end
    end

    self.eatenLastTurn = false

    if self.foodAvailable then
        Print("The floating pieces of fish food are quickly eaten by other fish in the tank.")
        self.foodAvailable = false
    elseif Random() < 0.15 then
        Print("The overhead dispenser drops pieces of delicious fish food to the water!")
        self.foodAvailable = true
    end

    self.numTurns = self.numTurns + 1
end

function app:HandleInput(input)
    -- Lowercase and trim
    local inputLower = input:lower():gsub("^%s+", ""):gsub("%s+$", "")

    if inputLower == "" then
        Print("Empty input given!")
        return
    end

    if inputLower == "quit" or inputLower == "exit" then
        GetSubsystem("Engine"):Exit()
    elseif self.gameOn then
        -- Game is on
        if inputLower == "help" then
            Print("The following commands are available: 'eat', 'hide', 'wait', 'score', 'quit'.")
        elseif inputLower == "score" then
            Print("You have survived " .. self.numTurns .. " turns.")
        elseif inputLower == "eat" then
            if self.foodAvailable then
                Print("You eat several pieces of fish food.")
                self.foodAvailable = false
                self.eatenLastTurn = true
                self.hunger = self.hunger - ((self.hunger > 3) and 2 or 1)
                if self.hunger < 0 then
                    self:EndGame("You have killed yourself by over-eating!")
                    return
                else
                    Print("You are now " .. hungerLevels[self.hunger + 1] .. ".")
                end
            else
                Print("There is no food available.")
            end

            self:Advance()
        elseif inputLower == "wait" then
            Print("Time passes...")
            self:Advance()
        elseif inputLower == "hide" then
            if self.urhoThreat > 0 then
                local evadeSuccess = self.hunger > 2 or Random() < 0.5
                if evadeSuccess then
                    Print("You hide behind the thick bottom vegetation, until Urho grows bored.")
                    self.urhoThreat = -2
                else
                    Print("Your movements are too slow; you are unable to hide from Urho.")
                end
            else
                Print("There is nothing to hide from.")
            end

            self:Advance()
        else
            Print("Cannot understand the input '" .. input .. "'.")
        end
    else
        -- Game is over, wait for (y)es or (n)o reply
        if inputLower:sub(1, 1) == "y" then
            self:StartGame()
        elseif inputLower:sub(1, 1) == "n" then
            GetSubsystem("Engine"):Exit()
        else
            Print("Please answer 'y' or 'n'.")
        end
    end
end

app:Run()
