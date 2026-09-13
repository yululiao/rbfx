//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

// Offline bundler for Lua scripts: turns .lua sources into .luc containers that the engine can
// load through LuaFile, optionally precompiled to bytecode and optionally encrypted.
//
// It exists so that the runtime never has to write a file or run a compiler: shipping a project
// means running this over its script folder, and the result is what a release build reads. The
// format itself lives in RbfxLuaScript (LuaScriptContainer.h), which this tool links against -
// one definition of the layout, so the tool and the engine cannot drift apart.
//
// Exit code is zero only when every input produced a container that this process could read
// back, which is what makes it usable as a build step: a broken script or a key mismatch fails
// the build instead of the game.

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Urho3D/Container/Ptr.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>

#include <LuaScript/LuaScriptContainer.h>

#include <cstring>
#include <cstdint>
#include <stdio.h>

using namespace Urho3D;

namespace
{

struct Job
{
    /// Absolute path of the source file.
    ea::string source;
    /// Absolute path the container is written to.
    ea::string destination;
    /// Name the runtime will know the resource by, used to seed the counter block.
    ea::string resourceName;
};

struct Options
{
    ea::vector<ea::string> inputs;
    ea::string output;
    bool recursive = false;
    bool encrypt = false;
    bool bytecode = false;
    bool strip = false;
    bool quiet = false;
    bool selfTestOnly = false;
    bool help = false;
    ea::string keyHex;
};

/// Parse exactly LuaScriptContainerKeyBytes*2 hex digits into a key.
bool ParseKey(const ea::string& hex, uint8_t* out)
{
    if (hex.length() != LuaScriptContainerKeyBytes * 2)
        return false;
    for (size_t i = 0; i < LuaScriptContainerKeyBytes; ++i)
    {
        uint8_t byte = 0;
        for (int nibble = 0; nibble < 2; ++nibble)
        {
            const char c = hex[i * 2 + (size_t)nibble];
            uint8_t value;
            if (c >= '0' && c <= '9')
                value = static_cast<uint8_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value = static_cast<uint8_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value = static_cast<uint8_t>(c - 'A' + 10);
            else
                return false;
            byte = static_cast<uint8_t>(byte << 4) | value;
        }
        out[i] = byte;
    }
    return true;
}

bool EndsWithSlash(const ea::string& path)
{
    return !path.empty() && (path[path.length() - 1] == '/' || path[path.length() - 1] == '\\');
}


void PrintUsage()
{
    printf("LuaCompiler - pack Lua scripts into .luc containers the engine can load.\n\n"
           "Usage: LuaCompiler [options] <input> [input...]\n\n"
           "  <input>              .lua file, or a directory containing them\n"
           "  -o, --out <path>     output file (single input) or root directory; defaults to\n"
           "                       writing .luc next to each .lua\n"
           "  -r, --recursive      descend into subdirectories of a directory input\n"
           "      --encrypt        encrypt with AES-256-CTR; without it the payload is stored\n"
           "                       in a plain container (still not readable as a stray edit)\n"
           "      --payload=K      K is 'source' (default) or 'bytecode'\n"
           "      --strip          drop debug info from bytecode; error messages lose lines\n"
           "      --key=<64hex>    content key for this run, overriding the compiled default\n"
           "      --selftest       verify the cipher and the container format, then exit\n"
           "  -q, --quiet          report failures only\n\n"
           "Notes:\n"
           "  * 'source' ships encrypted text and compiles on the target. It is the portable\n"
           "    choice and the default: source does not have to be readable to be protected.\n"
           "  * 'bytecode' is only loadable by the same Lua version, build configuration and\n"
           "    object model that produced it, so every target needs its own set of containers.\n"
           "  * The key must match the one the runtime was built with, otherwise every script\n"
           "    fails to load at run time. Pass the same value to both.\n");
}

bool ReadFile(Context* context, const ea::string& path, ea::vector<uint8_t>& out, ea::string& error)
{
    out.clear();
    File file(context, path, FILE_READ);
    if (!file.IsOpen())
    {
        error = ToString("cannot open '%s'", path.c_str());
        return false;
    }
    const unsigned size = file.GetSize();
    out.resize(size);
    if (size != 0 && file.Read(out.data(), size) != size)
    {
        out.clear();
        error = ToString("cannot read '%s'", path.c_str());
        return false;
    }
    return true;
}
bool WriteFile(Context* context, const ea::string& path, const ea::vector<uint8_t>& data, ea::string& error)
{
    File file(context, path, FILE_WRITE);
    if (!file.IsOpen())
    {
        error = ToString("cannot write '%s'", path.c_str());
        return false;
    }
    if (!data.empty() && file.Write(data.data(), static_cast<unsigned>(data.size())) != data.size())
    {
        error = ToString("short write to '%s'", path.c_str());
        return false;
    }
    return true;
}

/// Collect the .lua files of one input argument. A file input contributes itself; a directory
/// contributes its top level, plus deeper levels when \p recursive is set.
void CollectJobs(
    FileSystem* fs,
    const ea::string& root,
    const ea::string& relative,
    const Options& options,
    ea::vector<Job>& jobs)
{
    ea::vector<ea::string> files;
    fs->ScanDir(files, root, "*.lua", SCAN_FILES);
    for (const ea::string& file : files)
    {
        Job job;
        job.source = AddTrailingSlash(root) + file;
        job.resourceName = relative + ReplaceExtension(file, ".luc");
        jobs.push_back(job);
    }

    if (!options.recursive)
        return;

    ea::vector<ea::string> directories;
    fs->ScanDir(directories, root, "*", SCAN_DIRS);
    for (const ea::string& directory : directories)
    {
        // Dot folders are editor or version control leftovers, never script sources.
        if (directory.empty() || directory[0] == '.')
            continue;
        CollectJobs(fs, AddTrailingSlash(root) + directory, relative + directory + "/", options, jobs);
    }
}

/// Work out where each job's container goes, and create the folders that hold them.
void PlanDestinations(FileSystem* fs, const Options& options, ea::vector<Job>& jobs)
{
    if (options.output.empty())
    {
        // In place: Data/Scripts/main.lua becomes Data/Scripts/main.luc, which the loader picks
        // over the source automatically. Convenient for a one-off, and exactly what a pak built
        // straight from the project directory expects.
        for (Job& job : jobs)
            job.destination = ReplaceExtension(job.source, ".luc");
        return;
    }

    const bool outputIsDirectory = EndsWithSlash(options.output) || fs->DirExists(options.output);
    if (!outputIsDirectory)
    {
        // A single file target: only meaningful for a single job, and saying so beats silently
        // letting every later job overwrite the same file.
        const ea::string single = GetAbsolutePath(options.output, fs->GetCurrentDir());
        for (size_t i = 0; i < jobs.size(); ++i)
            jobs[i].destination = i == 0 ? single : ReplaceExtension(jobs[i].source, ".luc");
        if (!fs->CreateDirsRecursive(GetPath(single)))
        {
            printf("error: cannot create the output directory of '%s'\n", single.c_str());
        }
        return;
    }

    const ea::string outputRoot = AddTrailingSlash(GetAbsolutePath(options.output, fs->GetCurrentDir()));
    for (Job& job : jobs)
    {
        job.destination = outputRoot + job.resourceName;
        if (!fs->CreateDirsRecursive(GetPath(job.destination)))
            printf("error: cannot create the output directory of '%s'\n", job.destination.c_str());
    }
}

bool ProcessJob(Context* context, const Options& options, const Job& job, ea::string& error)
{
    ea::vector<uint8_t> source;
    if (!ReadFile(context, job.source, source, error))
        return false;

    // A UTF-8 BOM would make the loader fail with a message pointing at line one, character
    // one, which nobody can act on. Editors on Windows write them, so they get removed here.
    size_t bom = 0;
    if (source.size() >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF)
        bom = 3;

    ea::vector<uint8_t> payload;
    payload.assign(source.begin() + bom, source.end());
    LuaScriptPayload kind = LuaScriptPayload::Source;

    if (options.bytecode)
    {
        // The chunk name becomes part of the bytecode, and with it every error message the
        // script can produce at run time, so use the name the runtime will know it by.
        ea::string chunkName = ea::string("@") + job.resourceName;
        ea::vector<uint8_t> compiled;
        if (!LuaScriptCompileToBytecode(payload.data(), payload.size(), chunkName.c_str(), options.strip, compiled, error))
        {
            error = ToString("%s: %s", job.source.c_str(), error.c_str());
            return false;
        }
        payload = ea::move(compiled);
        kind = LuaScriptPayload::Bytecode;
    }

    const uint32_t ivSeed = LuaScriptContainerNameSeed(job.resourceName.c_str());
    const LuaScriptCipher cipher = options.encrypt ? LuaScriptCipher::Aes256Ctr : LuaScriptCipher::Plain;

    ea::vector<uint8_t> packed;
    if (!LuaScriptContainerPack(payload.data(), payload.size(), kind, cipher, ivSeed, packed, error))
    {
        error = ToString("%s: %s", job.source.c_str(), error.c_str());
        return false;
    }

    // Read it back before it is trusted: this catches a key that does not match the runtime's,
    // a container that the loader would reject, and a truncation, all at build time.
    ea::vector<uint8_t> unpacked;
    if (!LuaScriptContainerUnpack(packed.data(), packed.size(), unpacked, error))
    {
        error = ToString("%s: the container this tool produced cannot be read back: %s", job.source.c_str(), error.c_str());
        return false;
    }
    if (unpacked.size() != payload.size() || (payload.size() != 0 && memcmp(unpacked.data(), payload.data(), payload.size()) != 0))
    {
        error = ToString("%s: container round trip mismatch", job.source.c_str());
        return false;
    }

    return WriteFile(context, job.destination, packed, error);
}

bool ParseArguments(const ea::vector<ea::string>& arguments, Options& options, ea::string& error)
{
    // Urho's own command line splitter already removes the executable name, so every element
    // here is a parameter - starting at one would silently swallow the first argument.
    for (size_t i = 0; i < arguments.size(); ++i)
    {
        const ea::string& argument = arguments[i];

        if (argument == "-h" || argument == "--help")
        {
            options.help = true;
            return true;
        }
        if (argument == "-q" || argument == "--quiet")
            options.quiet = true;
        else if (argument == "-r" || argument == "--recursive")
            options.recursive = true;
        else if (argument == "--encrypt")
            options.encrypt = true;
        else if (argument == "--strip")
            options.strip = true;
        else if (argument == "--selftest")
            options.selfTestOnly = true;
        else if (argument == "-o" || argument == "--out")
        {
            if (++i >= arguments.size())
            {
                error = "-o expects a path";
                return false;
            }
            options.output = GetInternalPath(arguments[i]);
        }
        else if (argument.starts_with("--out="))
            options.output = GetInternalPath(argument.substr(6));
        else if (argument.starts_with("--payload="))
        {
            const ea::string payload = argument.substr(10);
            if (payload == "source")
                options.bytecode = false;
            else if (payload == "bytecode")
                options.bytecode = true;
            else
            {
                error = ToString("unknown payload '%s', expected 'source' or 'bytecode'", payload.c_str());
                return false;
            }
        }
        else if (argument.starts_with("--key="))
            options.keyHex = argument.substr(6);
        else if (argument.starts_with("-"))
        {
            error = ToString("unknown option '%s'", argument.c_str());
            return false;
        }
        else
            options.inputs.push_back(GetInternalPath(argument));
    }

    if (!options.selfTestOnly && options.inputs.empty())
    {
        error = "no input given";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    SharedPtr<Context> context(new Context());
    SharedPtr<FileSystem> fileSystem(new FileSystem(context));
    auto* fs = fileSystem.Get();

#ifdef WIN32
    ea::vector<ea::string> arguments = ParseArguments(GetCommandLineW());
#else
    ea::vector<ea::string> arguments = ParseArguments(argc, argv);
#endif

    Options options;
    ea::string error;
    if (!ParseArguments(arguments, options, error))
    {
        if (!error.empty())
            fprintf(stderr, "LuaCompiler: %s\n", error.c_str());
        return 1;
    }

    if (options.help)
    {
        PrintUsage();
        return 0;
    }

    if (!options.keyHex.empty())
    {
        uint8_t key[LuaScriptContainerKeyBytes];
        if (!ParseKey(options.keyHex, key))
        {
            fprintf(stderr, "LuaCompiler: --key expects %u hex digits\n",
                static_cast<unsigned>(LuaScriptContainerKeyBytes * 2));
            return 1;
        }
        LuaScriptContainerSetKey(key);
    }

    if (options.selfTestOnly)
    {
        if (!LuaScriptContainerSelfTest(error))
        {
            fprintf(stderr, "LuaCompiler: self test failed: %s\n", error.c_str());
            return 1;
        }
        if (!options.quiet)
            printf("LuaCompiler: cipher and container self test passed\n");
        return 0;
    }

    ea::vector<Job> jobs;
    for (const ea::string& input : options.inputs)
    {
        const ea::string absolute = GetAbsolutePath(input, fs->GetCurrentDir());
        if (fs->DirExists(AddTrailingSlash(absolute)))
        {
            CollectJobs(fs, AddTrailingSlash(absolute), EMPTY_STRING, options, jobs);
        }
        else if (fs->FileExists(absolute))
        {
            Job job;
            job.source = absolute;
            job.resourceName = GetFileName(ReplaceExtension(absolute, ".luc"));
            jobs.push_back(job);
        }
        else
        {
            fprintf(stderr, "LuaCompiler: '%s' is neither a file nor a directory\n", input.c_str());
            return 1;
        }
    }

    if (jobs.empty())
    {
        if (!options.quiet)
            printf("LuaCompiler: no .lua files found\n");
        return 0;
    }

    PlanDestinations(fs, options, jobs);

    if (options.bytecode && !options.quiet)
    {
        printf("warning: bytecode containers are tied to this Lua build; produce a separate set per target platform\n");
    }

    unsigned failures = 0;
    for (const Job& job : jobs)
    {
        if (ProcessJob(context.Get(), options, job, error))
        {
            if (!options.quiet)
                printf("%s -> %s\n", job.source.c_str(), job.destination.c_str());
        }
        else
        {
            fprintf(stderr, "LuaCompiler: %s\n", error.c_str());
            ++failures;
        }
    }

    if (!options.quiet)
    {
        printf("LuaCompiler: %u of %u file(s) packed (%s, %s)\n",
            static_cast<unsigned>(jobs.size() - failures),
            static_cast<unsigned>(jobs.size()),
            options.bytecode ? "bytecode" : "source",
            options.encrypt ? "encrypted" : "plain container");
    }
    return failures == 0 ? 0 : 1;
}
