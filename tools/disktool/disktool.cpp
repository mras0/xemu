#include "disk_image.h"
#include "util.h"
#include "fileio.h"
#include <print>
#include <queue>

namespace {

[[maybe_unused]] void printEntries(std::string_view path, const std::vector<DirectoryEntry>& des)
{
    std::println("Directory entries in {:?}", path);
    for (const auto& e : des) {
        if (e.attribute & DE_ATTR_MASK_DIRECTORY)
            std::print("{:9}", "<DIR>");
        else
            std::print("{:9}", e.size);

        auto dt = DateTimeFromEpochTime(e.modTime);
        std::print(" {}/{:02}/{:02} {:02}:{:02}:{:02}", dt.year, dt.month, dt.day, dt.hours, dt.minutes, dt.seconds);

        std::println(" {}", e.filename);
    }
}


[[maybe_unused]] void dir(DiskImage& img, std::string_view path)
{
    printEntries(path, img.dirEntries(path));
}

[[maybe_unused]] void dirRecursive(DiskImage& img)
{
    std::queue<std::string> q;
    q.push("\\");
    while (!q.empty()) {
        auto path = q.front();
        q.pop();
        const auto entries = img.dirEntries(path);
        printEntries(path, entries);
        for (const auto& de : entries) {
            if ((de.attribute & DE_ATTR_MASK_DIRECTORY) && de.filename[0] != '.')
                q.push(path + de.filename + "\\");
        }
    }
}

} // unnamed namespace

int main()
{
    try {
        DiskData diskData;
        const auto format = diskFormat1440K;
        unlink("test.img");
        CreateDisk("test.img", format);
        diskData.insert("test.img");
        FormatDosDisk(diskData);

        //diskData.insert("../../hd.bin");
        //diskData.insert(ReadFile(R"(c:\prog\xemu\misc\SW\FreeDos\x86BOOT.img)"));
        DiskImage img { diskData };
        //std::uint8_t bootSec[512];
        //diskData.read(bootSec, 0, sizeof(bootSec));
        //HexDump(0, bootSec, sizeof(bootSec));

        const char text[] = "Hello world!\r\nLine2\r\n";
        img.makeDir("\\temp");
        for (int i = 0; i < 100; ++i)
            img.writeFile(std::format("\\temp\\test{:03d}.txt", i), text, sizeof(text) - 1);

        dirRecursive(img);
        //auto data = img.readFile("\\freedos\\configs\\config.def");
        //HexDump(0, data.data(), data.size());

        #if 0
        const auto data = ReadFile(R"(c:\prog\xemu\misc\SW\Doom2.zip)");
        const std::string filePath = "temp\\doom2.zip";
        img.writeFile(filePath, data.data(), static_cast<uint32_t>(data.size()));
        dir(img, "temp");
        const auto readBack = img.readFile(filePath);
        if (readBack != data) {
            std::println("Failed to read back file!");
            exit(1);
        }
        #endif

    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}