#include "disk_image.h"
#include "util.h"
#include <print>
#include <stdexcept>
#include <algorithm>
#include <cstring>

#define LOG(...) std::println("DiskImage: " __VA_ARGS__)
#define ERROR(...) throw std::runtime_error{ std::format("DiskImage: " __VA_ARGS__) };

namespace {

constexpr uint8_t partIdDosExtended = 0x05; // DOS 3.3+ Extended Partition
constexpr uint8_t partIdDosFat16 = 0x06; // DOS 3.31+ 16-bit FAT (over 32M)

enum class PartitionType {
    FAT12,
    FAT16,
    FAT32,
};

PartitionType TypeFromIdentifier(uint8_t id)
{
    // https://aeb.win.tue.nl/partitions/partition_types-1.html
    switch (id) {
    case partIdDosFat16:
        return PartitionType::FAT16;
    default:
        ERROR("Unknown partition identifier {:02X}", id);
    }
}

const char* PartitionTypeString(PartitionType t)
{
    switch (t) {
    case PartitionType::FAT12:
        return "FAT12";
    case PartitionType::FAT16:
        return "FAT16";
    case PartitionType::FAT32:
        return "FAT32";
    default:
        return "Invalid partition type";
    }
}

static constexpr uint32_t RootClusterNumber = 0;
static constexpr uint32_t MinClusterNumber = 2;
static constexpr uint32_t EndClusterNumber = 0xffffffff;

static constexpr uint8_t DeletedEntryMarker = 0xE5;

enum {
    BPB_OFS_JUMP_INS = 0x000,
    BPB_OFS_OEMNAME = 0x003, // 8 chars
    BPB_OFS_BYTES_PER_SEC = 0x0B, // word
    BPB_OFS_SECS_PER_CLUSTER = 0x00D, // byte
    BPB_OFS_RESERVED_SECTORS = 0x00E, // word
    BPB_OFS_NUM_FATS = 0x010, // byte
    BPB_OFS_MAX_ROOT_ENTRIES = 0x011, // word
    BPB_OFS_TOTAL_SECTORS = 0x013, // word
    BPB_OFS_MEDIA_DESCRIPTOR = 0x015, // byte
    BPB_OFS_SECTORS_PER_FAT = 0x016, // word
    // DOS 3.31 BPB
    BPB_OFS_SECS_PER_TRACK = 0x018, // word
    BPB_OFS_NUM_HEADS = 0x01A, // word
    BPB_OFS_HIDDEN_SECTORS = 0x01C, // dword
    BPB_OFS_TOTAL_SECTORS_LARGE = 0x020, // dword
    // Extended BIOS Parameter Block (OS/2 / DOS 4.0)
    BPB_OFS_DRIVENUM = 0x024, // byte
    BPB_OFS_RESERVED = 0x025, // byte
    BPB_OFS_BOOTSIG = 0x026, // byte
    BPB_OFS_SERIAL_NO = 0x027, // dword
    BPB_OFS_VOLUME_ID = 0x02B, // 11 chars
    BPB_OFS_FS_TYPE = 0x036, // 8 chars
    BPB_OFS_EBPB_END = 0x03E,

    BPB_OFS_MAGIC = 0x1FE, // word (0xAA55)
};

constexpr uint16_t BOOT_MAGIC = 0xAA55;

struct FATDirEntry {
    char name[8];
    char extension[3];
    uint8_t attributes; // 0x0B
    uint8_t reserved1; // 0x0C
    uint8_t reserved2; // 0x0D
    uint16_t reserved3; // 0x0E
    uint16_t reserved4; // 0x10
    uint16_t reserved5; // 0x12
    uint16_t clusterHigh; // 0x14 (FAT32 only)
    uint16_t modifyTime; // 0x16
    uint16_t modifyDate; // 0x18
    uint16_t clusterLow; // 0x1A
    uint32_t fileSize; // 0x1C

    bool deleted() const
    {
        return static_cast<uint8_t>(name[0]) == DeletedEntryMarker;
    }

    std::string filename() const
    {
        // TODO: Should really convert from "OEM" to UTF-8...
        std::string fn { name, name + sizeof(name) };
        if (fn[0] == 0x05) // 5 means first char was actually 0xE5 (deleted entry marker)
            fn[0] = DeletedEntryMarker;
        while (!fn.empty() && fn.back() == ' ')
            fn.pop_back();
        fn += '.';
        fn += std::string { extension, extension + sizeof(extension) };
        while (!fn.empty() && fn.back() == ' ')
            fn.pop_back();
        if (fn.back() == '.')
            fn.pop_back();
        return fn;
    }
};
static_assert(sizeof(FATDirEntry) == 32);

std::string FormatDosTime(uint16_t time)
{
    return std::format("{:02d}:{:02d}:{:02d}", time >> 11, (time >> 5) & 0x1f, 2 * (time & 0x1f));
}

std::string FormatDosDate(uint16_t date)
{
    return std::format("{:02d}/{:02d}/{:04d}", date & 0x1f, (date >> 5) & 0xf, 1980 + (date >> 9));
}

[[maybe_unused]] std::string FormatDosDateTime(uint16_t date, uint16_t time)
{
    return std::format("{} {}", FormatDosDate(date), FormatDosTime(time));
}

int64_t DosTimeToEpochTime(uint16_t date, uint16_t time)
{
    return SecondsSinceEpoch(1980 + (date >> 9), (date >> 5) & 0xf, date & 0x1f, time >> 11, (time >> 5) & 0x3f, 2 * (time & 0x1f));
}

void SetModifyTime(FATDirEntry& de, int64_t t)
{
    const auto dt = DateTimeFromEpochTime(t);
    de.modifyDate = static_cast<uint16_t>((dt.year - 1980) << 9 | dt.month << 5 | dt.day);
    de.modifyTime = static_cast<uint16_t>(dt.hours << 11 | dt.minutes << 5 | (dt.seconds >> 1));
}

std::string ConvertUC2toUTF8(const uint16_t* lfn, size_t len)
{
    std::string filename;
    for (size_t i = 0; i < len; ++i) {
        const auto ch = lfn[i];
        if (!ch)
            break;
        if (ch < 0x80) {
            filename.push_back(static_cast<uint8_t>(ch));
        } else if (ch < 0x800) {
            filename.push_back(static_cast<uint8_t>(0xC0 | (ch >> 6)));
            filename.push_back(static_cast<uint8_t>(0x80 | (ch & 0x3f)));
        } else {
            filename.push_back(static_cast<uint8_t>(0xE0 | (ch >> 12)));
            filename.push_back(static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3f)));
            filename.push_back(static_cast<uint8_t>(0x80 | (ch & 0x3f)));
        }
    }
    return filename;
}

std::pair<std::string_view, std::string_view> GetPathComponent(std::string_view in)
{
    while (!in.empty() && (in[0] == '\\' || in[0] == '/'))
        in = in.substr(1);
    size_t compStart = 0;
    while (compStart < in.length() && (in[compStart] != '\\' && in[compStart] != '/'))
        ++compStart;
    return { in.substr(0, compStart), in.substr(compStart) };
}

std::pair<std::string_view, std::string_view> GetPathDirName(std::string_view in)
{
    for (size_t i = in.length(); i--;) {
        if (in[i] == '\\' || in[i] == '/')
            return { in.substr(0, i), in.substr(i + 1) };
    }
    ERROR("Invalid path {:?}", in);
}

uint8_t ToUpperAscii(uint8_t ch)
{
    return ch >= 'a' && ch <= 'z' ? ch & 0xdf : ch;
}

bool ValidFileChar(uint8_t ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return true;
    if (ch >= '0' && ch <= '9')
        return true;
    switch (ch) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '-':
    case '@':
    case '^':
    case '_':
    case '`':
    case '{':
    case '}':
    case '~':
        return true;
    }
    return false;
}

bool PathComponentEqual(std::string_view l, std::string_view r)
{
    if (l.length() != r.length())
        return false;
    for (size_t i = 0; i < l.length(); ++i) {
        if (l[i] == r[i])
            continue;
        // TODO: Handle UTF8 correctly
        if ((l[i] | r[i]) & 0x80)
            ERROR("TODO: Proper comparison for {:?} and {:?}", l, r);
        if (ToUpperAscii(l[i]) != ToUpperAscii(r[i]))
            return false;
    }
    return true;
}

bool FilenameEqual(const FATDirEntry& l, const FATDirEntry& r)
{
    if (!PathComponentEqual(std::string_view(l.name, l.name + sizeof(l.name)), std::string_view(r.name, r.name + sizeof(r.name))))
        return false;
    return PathComponentEqual(std::string_view(l.extension, l.extension + sizeof(l.extension)), std::string_view(r.extension, r.extension + sizeof(r.extension)));
}

const DirectoryEntry* FindInEntries(std::string_view component, const std::vector<DirectoryEntry>& entries)
{
    for (const auto& ent : entries) {
        if (PathComponentEqual(component, ent.filename))
            return &ent;
    }
    return nullptr;
}

bool ConvertFilenamePart(char* dest, size_t destLen, std::string_view sv)
{
    size_t dstIdx = 0;
    size_t srcIdx = 0;
    bool allValid = true;
    while (dstIdx < destLen && srcIdx < sv.length()) {
        uint8_t ch = ToUpperAscii(sv[srcIdx++]);
        if (ValidFileChar(ch))
            dest[dstIdx++] = ch;
        else
            allValid = false;
    }
    if (srcIdx != sv.length())
        allValid = false;
    return allValid;
}

bool SetFilename(FATDirEntry& de, std::string_view sv)
{
    bool valid;
    std::memset(de.name, ' ', sizeof(de.name));
    std::memset(de.extension, ' ', sizeof(de.extension));
    auto dotIdx = sv.find_first_of('.');
    if (dotIdx != std::string_view::npos) {
        valid = ConvertFilenamePart(de.name, sizeof(de.name), sv.substr(0, dotIdx));
        if (dotIdx != sv.length() - 1)
            valid &= ConvertFilenamePart(de.extension, sizeof(de.extension), sv.substr(dotIdx + 1));
    } else {
        valid = ConvertFilenamePart(de.name, sizeof(de.name), sv);
    }
    return valid;
}

void SetFilenameRaw(FATDirEntry& de, std::string_view sv)
{
    if (sv.length() > 11)
        ERROR("Invalid raw filename {:?}", sv);
    std::memset(&de, ' ', 11);
    std::memcpy(&de, sv.data(), sv.length());
}

struct PartitionInfo {
    uint32_t startLBA;
    uint32_t numSectors;
    PartitionType type;
};

struct ParsedPartionInfo {
    uint32_t startLBA;
    uint32_t numSectors;
    uint8_t type;
};

struct FATInfo {
    uint16_t bytesPerSector;
    uint8_t secsPerCluster;
    uint16_t numReservedSectors;
    uint8_t numFATs;
    uint16_t numRootDirEntries;
    uint32_t totalSectors;
    uint32_t sectorsPerFAT;

    std::vector<uint32_t> fat;

    uint32_t clusterBytes() const
    {
        return bytesPerSector * secsPerCluster;
    }

    uint32_t rootDirSectorOffset() const
    {
        return numReservedSectors + numFATs * sectorsPerFAT;
    }

    uint32_t rootDirSectorCount() const
    {
        return (numRootDirEntries * sizeof(FATDirEntry) + bytesPerSector - 1) / bytesPerSector;
    }

    uint32_t dataSectorOffset() const
    {
        return rootDirSectorOffset() + rootDirSectorCount();
    }

    bool validCluster(uint32_t cluster) const
    {
        return cluster >= MinClusterNumber && cluster < totalSectors / secsPerCluster;
    }
};

std::vector<DirectoryEntry> ParseDirectory(const FATDirEntry* de, uint32_t count, bool isFAT32 = false)
{
    std::vector<uint16_t> lfn;
    uint8_t lfnSeq = 0xFF;
    std::vector<DirectoryEntry> result;

    for (uint32_t i = 0; i < count; ++i) {
        const auto& fi = de[i];
        if (fi.name[0] == 0)
            break;
        if (fi.deleted())
            continue;
        if (fi.attributes & DE_ATTR_MASK_VOLUME_LABEL) {
            if (fi.attributes != 0x0f)
                continue;
            constexpr uint8_t charsPerEntry = 13;
            auto seq = static_cast<uint8_t>(fi.name[0]);
            if ((seq & 0x50) == 0x40) { // Last in filename (first in directory entry)
                lfn.resize((lfnSeq & 0xf) * charsPerEntry);
            } else if (lfnSeq != seq + 1) {
                LOG("Invalid LFN entry!");
                lfnSeq = 0xFF;
                continue;
            }
            lfnSeq = seq & 0xf;
            auto bs = reinterpret_cast<const uint8_t*>(&fi);
            uint16_t* dst = &lfn[(lfnSeq - 1) * charsPerEntry];
            for (int j = 0; j < 5; ++j)
                *dst++ = GetU16(&bs[0x01 + j * 2]);
            for (int j = 0; j < 6; ++j)
                *dst++ = GetU16(&bs[0x0E + j * 2]);
            for (int j = 0; j < 2; ++j)
                *dst++ = GetU16(&bs[0x1C + j * 2]);
            continue;
        }

        std::string filename = lfnSeq == 1 ? ConvertUC2toUTF8(&lfn[0], lfn.size()) : fi.filename();
        uint32_t cluster = fi.clusterLow;
        if (isFAT32)
            cluster |= fi.clusterHigh << 16;

        result.push_back({ std::move(filename), fi.fileSize, cluster, fi.attributes, DosTimeToEpochTime(fi.modifyDate, fi.modifyTime) });
        lfnSeq = 0xFF;
    }

    return result;
}

} // Unnamed namespace

class DiskImage::impl {
public:
    impl(DiskData& diskData);

    uint32_t numPartitions() const
    {
        return static_cast<uint32_t>(partitions_.size());
    }

    void setPartition(uint32_t index);
    std::vector<DirectoryEntry> dirEntries(std::string_view path);
    std::vector<uint8_t> readFile(std::string_view path);
    void writeFile(std::string_view path, const void* data, uint32_t size);
    void makeDir(std::string_view path);

private:
    DiskData& diskData_;
    std::vector<PartitionInfo> partitions_;
    size_t currentPartition_ = 0;
    FATInfo fatInfo_;

    size_t sectorByteOffset(uint32_t lbaOffset) const;
    size_t clusterByteOffset(uint32_t cluster) const;

    void readFatSectors(uint8_t* data, uint32_t lbaOffset, uint32_t count);
    void readFatCluster(uint8_t* data, uint32_t cluster);
    void writeFatSectors(const uint8_t* data, uint32_t lbaOffset, uint32_t count);
    void writeFatCluster(const uint8_t* data, uint32_t cluster);

    void writeFAT();
    void freeClusterChain(uint32_t cluster);

    std::vector<ParsedPartionInfo> parsePartitionInfo(uint32_t lba, bool extended);
    DirectoryEntry findEntry(std::string_view path);
    std::vector<FATDirEntry> readDir(uint32_t cluster);
    void writeDir(uint32_t cluster, const std::vector<FATDirEntry>& entries);
    std::vector<DirectoryEntry> dirEntries(uint32_t cluster);
    void addDirEntry(uint32_t dirCluster, const FATDirEntry& de);
    std::vector<uint32_t> allocClusters(uint32_t numClusters);

    uint32_t getCluster(const FATDirEntry& de)
    {
        if (const auto type = partitions_[currentPartition_].type; type != PartitionType::FAT12 && type != PartitionType::FAT16)
            ERROR("TODO: getCluster for {}", PartitionTypeString(type));
        return de.clusterLow;
    }

    void setCluster(FATDirEntry& de, uint32_t cluster)
    {
        if (const auto type = partitions_[currentPartition_].type; type != PartitionType::FAT12 && type != PartitionType::FAT16)
            ERROR("TODO: setCluster for {}", PartitionTypeString(type));
        de.clusterLow = static_cast<uint16_t>(cluster);
        de.clusterHigh = 0;
    }
};

DiskImage::impl::impl(DiskData& diskData)
    : diskData_ { diskData }
{
    uint8_t bootSec[bytesPerSector];
    diskData_.read(bootSec, 0, bytesPerSector);
    const bool hasJump = (bootSec[0] == 0xE9 || bootSec[0] == 0xEB);

    // TODO: Better detection logic. The identifiers (FAT12/16/32) are only for display purposes.
    if (hasJump && !std::memcmp(&bootSec[0x52], "FAT32   ", 8)) {
        ERROR("TODO: FAT32");
    } else if (hasJump && !std::memcmp(&bootSec[BPB_OFS_FS_TYPE], "FAT1", 4)) {
        LOG("Assuming flat disk. Type={:5.5s}", (char*)&bootSec[BPB_OFS_FS_TYPE]);
        partitions_.push_back(PartitionInfo { 0, static_cast<uint32_t>(diskData_.sizeInBytes() / bytesPerSector), bootSec[0x3A] == '2' ? PartitionType::FAT12 : PartitionType::FAT16 });
    } else {
        // Assume MBR is present
        auto pInfo = parsePartitionInfo(0, false);
        int extendedIdx = -1;
        for (int i = 0; i < static_cast<int>(pInfo.size()); ++i) {
            if (pInfo[i].type == partIdDosExtended) {
                if (extendedIdx != -1)
                    ERROR("More than one extended partition found!");
                extendedIdx = i;
            }
        }
        if (extendedIdx != -1) {
            const auto extendedPartitions = parsePartitionInfo(pInfo[extendedIdx].startLBA, true);
            pInfo.erase(pInfo.begin() + extendedIdx);
            pInfo.insert(pInfo.end(), extendedPartitions.begin(), extendedPartitions.end());
        }

        for (const auto& info : pInfo)
            partitions_.push_back(PartitionInfo { info.startLBA, info.numSectors, TypeFromIdentifier(info.type) });
    }

    setPartition(0);
}

std::vector<ParsedPartionInfo> DiskImage::impl::parsePartitionInfo(uint32_t lba, bool extended)
{
    uint8_t bootSec[bytesPerSector];
    diskData_.read(bootSec, static_cast<size_t>(lba) * bytesPerSector, bytesPerSector);

    const char* id = extended ? "Extended" : "Primary";

    if (auto bootSig = GetU16(&bootSec[BPB_OFS_MAGIC]); bootSig != BOOT_MAGIC)
        ERROR("{}/{} Invalid boot signature {:04X}", id, lba, bootSig);

    std::vector<ParsedPartionInfo> info;

    for (uint32_t partCnt = 0; partCnt < 4; ++partCnt) {
        const auto part = &bootSec[0x1BE + partCnt * 16];
        if (std::find_if(part, part + 16, [](uint8_t value) { return value != 0; }) == part + 16)
            continue;

        const auto status = part[0];
        const auto startHead = part[1];
        const auto startCyl = part[3] | (part[2] & 0xc0) << 2;
        const auto startSector = part[2] & 0x3f;
        const auto type = part[4];
        const auto endHead = part[5];
        const auto endCyl = part[7] | (part[6] & 0xc0) << 2;
        const auto endSector = part[7] & 0x3f;
        const auto startLBA = lba + GetU32(&part[8]);
        const auto numSectors = GetU32(&part[12]);

        LOG("Parition {}/{}/{} status {:02X} type {:02X} start {}/{}/{} end {}/{}/{} lba {} {}", id, partCnt, lba, status, type, startCyl, startHead, startSector, endCyl, endHead, endSector, startLBA, numSectors);
        if (const auto calcedStart = diskData_.format().toLBA(startCyl, startHead, startSector); calcedStart != startLBA)
            ERROR("Partition {}/{}/{} start {} != calculated {}", id, partCnt, lba, startLBA, calcedStart);

        if (const auto totalSectors = diskData_.format().totalSectors(); startLBA >= totalSectors || startLBA + numSectors > totalSectors)
            ERROR("Partition {}/{}/{} doesn't fit {} / {}-{}", id, partCnt, lba, totalSectors, startLBA, startLBA + numSectors);

        info.push_back(ParsedPartionInfo { startLBA, numSectors, type });
    }
    return info;
}

size_t DiskImage::impl::sectorByteOffset(uint32_t lbaOffset) const
{
    const auto& partInfo = partitions_[currentPartition_];
    return static_cast<size_t>(partInfo.startLBA + lbaOffset * fatInfo_.bytesPerSector / bytesPerSector) * bytesPerSector;
}

size_t DiskImage::impl::clusterByteOffset(uint32_t cluster) const
{
    const auto& partInfo = partitions_[currentPartition_];
    if (!fatInfo_.validCluster(cluster))
        ERROR("Invalid cluster {:X}", cluster);
    return static_cast<size_t>(partInfo.startLBA + (fatInfo_.dataSectorOffset() + (cluster - MinClusterNumber) * fatInfo_.secsPerCluster) * fatInfo_.bytesPerSector / bytesPerSector) * bytesPerSector;
}

void DiskImage::impl::readFatSectors(uint8_t* data, uint32_t lbaOffset, uint32_t count)
{
    diskData_.read(data, sectorByteOffset(lbaOffset), fatInfo_.bytesPerSector * count);
}

void DiskImage::impl::readFatCluster(uint8_t* data, uint32_t cluster)
{
    diskData_.read(data, clusterByteOffset(cluster), fatInfo_.clusterBytes());
}

void DiskImage::impl::writeFatSectors(const uint8_t* data, uint32_t lbaOffset, uint32_t count)
{
    diskData_.write(data, sectorByteOffset(lbaOffset), fatInfo_.bytesPerSector * count);
}

void DiskImage::impl::writeFatCluster(const uint8_t* data, uint32_t cluster)
{
    diskData_.write(data, clusterByteOffset(cluster), fatInfo_.clusterBytes());
}

void DiskImage::impl::setPartition(uint32_t index)
{
    if (index >= numPartitions())
        ERROR("Invalid partition {}", index);
    currentPartition_ = index;

    uint8_t bootSec[bytesPerSector];
    const auto& partInfo = partitions_[currentPartition_];
    diskData_.read(bootSec, static_cast<size_t>(partInfo.startLBA) * bytesPerSector, bytesPerSector);


    fatInfo_.bytesPerSector = GetU16(&bootSec[BPB_OFS_BYTES_PER_SEC]);
    fatInfo_.secsPerCluster = bootSec[BPB_OFS_SECS_PER_CLUSTER];
    fatInfo_.numReservedSectors = GetU16(&bootSec[BPB_OFS_RESERVED_SECTORS]);
    fatInfo_.numFATs = bootSec[BPB_OFS_NUM_FATS];
    fatInfo_.numRootDirEntries = GetU16(&bootSec[BPB_OFS_MAX_ROOT_ENTRIES]);
    fatInfo_.totalSectors = GetU16(&bootSec[BPB_OFS_TOTAL_SECTORS]);
    if (!fatInfo_.totalSectors)
        fatInfo_.totalSectors = GetU32(&bootSec[BPB_OFS_TOTAL_SECTORS_LARGE]);
    fatInfo_.sectorsPerFAT = GetU16(&bootSec[BPB_OFS_SECTORS_PER_FAT]); // FAT32 stores this is a 32-bit value at 0x024

    if (fatInfo_.bytesPerSector != bytesPerSector)
        ERROR("Partition {}: Unsupported logical sector size {}", currentPartition_, fatInfo_.bytesPerSector);

    if (fatInfo_.totalSectors != partInfo.numSectors)
        ERROR("Partition {}: Wrong number of sectors {} / {}", currentPartition_, fatInfo_.totalSectors, partInfo.numSectors);

    if (!fatInfo_.numFATs)
        ERROR("Parition {}: No FATs?", currentPartition_);

    const auto fatSize = fatInfo_.sectorsPerFAT * fatInfo_.bytesPerSector;
    std::vector<uint8_t> tempBuf(fatSize);
    std::vector<uint8_t> buf(fatInfo_.bytesPerSector);
    readFatSectors(&tempBuf[0], fatInfo_.numReservedSectors, fatInfo_.sectorsPerFAT);

    for (uint32_t fatCnt = 1; fatCnt < fatInfo_.numFATs; ++fatCnt) {
        for (uint32_t i = 0; i < fatInfo_.sectorsPerFAT; ++i) {
            readFatSectors(&buf[0], fatInfo_.numReservedSectors + i + fatCnt * fatInfo_.sectorsPerFAT, 1);
            if (std::memcmp(&buf[0], &tempBuf[i * fatInfo_.bytesPerSector], fatInfo_.bytesPerSector))
                LOG("Warning mismatch in FAT {}! Sector={}", fatCnt, i);
        }
    }

    if (partInfo.type != PartitionType::FAT12 && partInfo.type != PartitionType::FAT16)
        ERROR("Parition {}: TODO support {}", currentPartition_, PartitionTypeString(partInfo.type));

    const uint32_t numClusters = partInfo.type == PartitionType::FAT12 ? fatSize * 2 / 3 : fatSize / 2;
    const uint32_t endCluster = partInfo.type == PartitionType::FAT12 ? 0xfff : 0xffff;
    fatInfo_.fat.resize(numClusters);
    for (uint32_t i = 0; i < numClusters; ++i) {
        if (partInfo.type == PartitionType::FAT12) {
            fatInfo_.fat[i] = GetU16(&tempBuf[i * 3 / 2]);
            if (i & 1)
                fatInfo_.fat[i] >>= 4;
            fatInfo_.fat[i] &= 0xfff;
        } else {
            fatInfo_.fat[i] = GetU16(&tempBuf[i * 2]);
        }
        if (fatInfo_.fat[i] == endCluster)
            fatInfo_.fat[i] = EndClusterNumber;
    }
}

void DiskImage::impl::writeFAT()
{
    const auto fatSize = fatInfo_.sectorsPerFAT * fatInfo_.bytesPerSector;
    std::vector<uint8_t> tempBuf(fatSize);

    const uint32_t numClusters = static_cast<uint32_t>(fatInfo_.fat.size());
    if (const auto type = partitions_[currentPartition_].type; type == PartitionType::FAT16) {
        for (uint32_t i = 0; i < numClusters; ++i)
            PutU16(&tempBuf[i * 2], static_cast<uint16_t>(fatInfo_.fat[i]));
    } else if (type == PartitionType::FAT12) {
        for (uint32_t i = 0; i < numClusters; ++i) {
            const auto cluster = static_cast<uint16_t>(fatInfo_.fat[i]);
            const auto offset = i * 3 / 2;
            if (i & 1) {
                tempBuf[offset] = (tempBuf[offset] & 0x0f) | static_cast<uint8_t>((cluster & 0xf) << 4);
                tempBuf[offset + 1 ] = static_cast<uint8_t>((cluster >> 4) & 0xff);
            } else {
                tempBuf[offset] = static_cast<uint8_t>(cluster & 0xff);
                tempBuf[offset + 1] = (tempBuf[offset + 1] & 0xf0) | static_cast<uint8_t>((cluster >> 8) & 0xff);
            }
        }
    } else {
        ERROR("TODO: support {} in writeFAT", PartitionTypeString(type));
    }
    
    for (uint32_t fatCnt = 0; fatCnt < fatInfo_.numFATs; ++fatCnt)
        writeFatSectors(&tempBuf[0], fatInfo_.numReservedSectors + fatCnt * fatInfo_.sectorsPerFAT, fatInfo_.sectorsPerFAT);
}

DirectoryEntry DiskImage::impl::findEntry(std::string_view path)
{
    const auto origPath = path;
    std::vector<DirectoryEntry> entries;
    DirectoryEntry de {};
    // Fake entry for root dir
    de.attribute = DE_ATTR_MASK_DIRECTORY;
    de.cluster = RootClusterNumber;
    for (;;) {
        const auto [component, next] = GetPathComponent(path);
        if (component.empty()) {
            assert(next.empty());
            return de;
        }

        entries = dirEntries(de.cluster);

        auto ent = FindInEntries(component, entries);
        if (!ent || !fatInfo_.validCluster(ent->cluster))
            ERROR("{:?} not found for {:?}", component, origPath);
        de = *ent;
        path = next;
    }
}

std::vector<DirectoryEntry> DiskImage::impl::dirEntries(uint32_t cluster)
{
    auto rawData = readDir(cluster);
    return ParseDirectory(rawData.data(), static_cast<uint32_t>(rawData.size()));
}

std::vector<FATDirEntry> DiskImage::impl::readDir(uint32_t cluster)
{
    if (cluster == RootClusterNumber) {
        std::vector<FATDirEntry> result(fatInfo_.rootDirSectorCount() * fatInfo_.bytesPerSector / sizeof(FATDirEntry));
        readFatSectors(reinterpret_cast<uint8_t*>(&result[0]), fatInfo_.rootDirSectorOffset(), fatInfo_.rootDirSectorCount());
        return result;
    }

    std::vector<FATDirEntry> result;
    for (; fatInfo_.validCluster(cluster); cluster = fatInfo_.fat[cluster]) {
        const auto oldSize = result.size();
        result.resize(oldSize + fatInfo_.clusterBytes() / sizeof(FATDirEntry));
        readFatCluster(reinterpret_cast<uint8_t*>(&result[oldSize]), cluster);
    }
    return result;
}

void DiskImage::impl::writeDir(uint32_t cluster, const std::vector<FATDirEntry>& entries)
{
    if (cluster == RootClusterNumber) {
        assert(entries.size() * sizeof(entries[0]) == fatInfo_.rootDirSectorCount() * fatInfo_.bytesPerSector);
        writeFatSectors(reinterpret_cast<const uint8_t*>(&entries[0]), fatInfo_.rootDirSectorOffset(), fatInfo_.rootDirSectorCount());
        return;
    }
    const auto entriesPerCluster = fatInfo_.clusterBytes() / sizeof(FATDirEntry);
    for (size_t i = 0; i < entries.size(); i += entriesPerCluster) {
        if (!fatInfo_.validCluster(cluster))
            ERROR("Internal error in writeDir: cluster chain");
        writeFatCluster(reinterpret_cast<const uint8_t*>(&entries[i]), cluster);
        cluster = fatInfo_.fat[cluster];
    }
    assert(!fatInfo_.validCluster(cluster));
}

std::vector<DirectoryEntry> DiskImage::impl::dirEntries(std::string_view path)
{
    return dirEntries(findEntry(path).cluster);
}

std::vector<uint8_t> DiskImage::impl::readFile(std::string_view path)
{
    const auto ent = findEntry(path);
    if (!fatInfo_.validCluster(ent.cluster) || (ent.attribute & DE_ATTR_MASK_DIRECTORY))
        ERROR("{:?} is not a valid file", path);

    std::vector<uint8_t> result;
    for (uint32_t cluster = ent.cluster; fatInfo_.validCluster(cluster); cluster = fatInfo_.fat[cluster]) {
        if (result.size() > ent.size)
            ERROR("Cluster chain invalid for {:?} -- Too long", path);
        const auto prevSize = result.size();
        result.resize(prevSize + fatInfo_.clusterBytes()); 
        readFatCluster(&result[prevSize], cluster);
    }
    if (result.size() < ent.size)
        ERROR("Cluster chain invalid for {:?} -- Too short", path);
    result.resize(ent.size);

    return result;
}

std::vector<uint32_t> DiskImage::impl::allocClusters(uint32_t numClusters)
{
    std::vector<uint32_t> clusters;
    for (uint32_t i = MinClusterNumber;; ++i) {
        if (clusters.size() == numClusters)
            break;
        if (!fatInfo_.validCluster(i))
            ERROR("Out of disk space");

        if (const auto cluster = fatInfo_.fat[i]; cluster < MinClusterNumber)
            clusters.push_back(i);
    }
    return clusters;
}

void DiskImage::impl::makeDir(std::string_view path)
{
    while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
        path = path.substr(0, path.size() - 1);
    auto [dir, filename] = GetPathDirName(path);
    const auto dirEntry = findEntry(dir);
    if (!(dirEntry.attribute & DE_ATTR_MASK_DIRECTORY))
        ERROR("{:?} is not a directory", dir);
    const auto dirCluster = dirEntry.cluster;
    FATDirEntry de {};
    if (!SetFilename(de, filename))
        ERROR("TODO: Support LFN for {:?}", filename);
    SetModifyTime(de, CurrentEpochLocalTime());
    de.attributes = DE_ATTR_MASK_DIRECTORY;
    const auto cluster = allocClusters(1)[0];
    setCluster(de, cluster);

    std::vector<FATDirEntry> dirEntries(fatInfo_.clusterBytes() / sizeof(FATDirEntry));
    
    SetFilenameRaw(dirEntries[0], ".");
    dirEntries[0].attributes = DE_ATTR_MASK_DIRECTORY;
    setCluster(dirEntries[0], cluster);
    dirEntries[0].modifyDate = de.modifyDate;
    dirEntries[0].modifyTime = de.modifyTime;

    SetFilenameRaw(dirEntries[1], "..");
    dirEntries[1].attributes = DE_ATTR_MASK_DIRECTORY;
    setCluster(dirEntries[1], dirCluster);
    dirEntries[1].modifyDate = de.modifyDate;
    dirEntries[1].modifyTime = de.modifyTime;

    writeFatCluster(reinterpret_cast<const uint8_t*>(&dirEntries[0]), cluster);

    fatInfo_.fat[cluster] = EndClusterNumber;
    writeFAT();
    addDirEntry(dirCluster, de);
}

void DiskImage::impl::writeFile(std::string_view path, const void* data, uint32_t size)
{
    auto [dir, filename] = GetPathDirName(path);
    //LOG("dir={:?} filename={:?}", dir, filename);

    const auto dirEntry = findEntry(dir);
    if (!(dirEntry.attribute & DE_ATTR_MASK_DIRECTORY))
        ERROR("{:?} is not a directory", dir);
    const auto dirCluster = dirEntry.cluster;

    // Prepare directory entry
    FATDirEntry de {};
    if (!SetFilename(de, filename))
        ERROR("TODO: Support LFN for {:?}", filename);
    SetModifyTime(de, CurrentEpochLocalTime());
    de.fileSize = size;

    // Allocate clusters for file
    const auto numClusters = (size + fatInfo_.clusterBytes() - 1) / fatInfo_.clusterBytes();
    std::vector<uint32_t> clusters = allocClusters(numClusters);

    // Write data to clusters
    for (size_t clusterIdx = 0; clusterIdx < numClusters; ++clusterIdx) {
        const auto src = reinterpret_cast<const uint8_t*>(data) + clusterIdx * fatInfo_.clusterBytes();
        const auto cluster = clusters[clusterIdx];
        const bool last = clusterIdx == clusters.size() - 1;
        if (last && size % fatInfo_.clusterBytes()) {
            // Final cluster is partial
            std::vector<uint8_t> buf(fatInfo_.clusterBytes());
            std::memcpy(&buf[0], src, size % fatInfo_.clusterBytes());
            writeFatCluster(&buf[0], cluster);
        } else {
            writeFatCluster(src, cluster);
        }

        // Update FAT chain
        fatInfo_.fat[cluster] = last ? EndClusterNumber : clusters[clusterIdx + 1];
    }

    if (numClusters)
        setCluster(de, clusters[0]);

    writeFAT();

    // Add directory entry
    addDirEntry(dirCluster, de);
}

void DiskImage::impl::addDirEntry(uint32_t dirCluster, const FATDirEntry& de)
{
    auto dir = readDir(dirCluster);
    constexpr size_t invalidEntry = ~size_t(0);
    size_t freeEntry = invalidEntry;
    size_t deletedEntry = invalidEntry;
    size_t entry = invalidEntry;
    for (size_t i = 0; i < dir.size(); ++i) {
        if (dir[i].name[0] == 0) {
            if (freeEntry == invalidEntry)
                freeEntry = i;
            break;
        } else if (dir[i].deleted()) {
            if (deletedEntry == invalidEntry)
                deletedEntry = i;
        } else if (FilenameEqual(dir[i], de)) {
            // Free cluster chain!
            const auto clusterChain = getCluster(de);
            if (clusterChain) {
                LOG("{:?} already exists, freeing cluster chain", de.filename());
                freeClusterChain(clusterChain);
            } 
            entry = i;
            break;
        }
    }

    if (entry == invalidEntry)
        entry = freeEntry;
    if (entry == invalidEntry)
        entry = deletedEntry;

    if (entry == invalidEntry) {
        if (dirCluster == RootClusterNumber)
            ERROR("Root directory is full");
        // Grow directory by another cluster
        entry = static_cast<uint32_t>(dir.size());
        dir.resize(dir.size() + fatInfo_.clusterBytes() / sizeof(FATDirEntry));
        const auto newCluster = allocClusters(1)[0];
        fatInfo_.fat[newCluster] = EndClusterNumber;
        for (uint32_t cluster = dirCluster;; cluster = fatInfo_.fat[cluster]) {
            if (fatInfo_.fat[cluster] == EndClusterNumber) {
                fatInfo_.fat[cluster] = newCluster;
                break;
            }
        }
        writeFAT();
    }

    std::memcpy(&dir[entry], &de, sizeof(de));
    writeDir(dirCluster, dir);
}

void DiskImage::impl::freeClusterChain(uint32_t cluster)
{
    for (uint32_t nextCluster; fatInfo_.validCluster(cluster); cluster = nextCluster) {
        nextCluster = fatInfo_.fat[cluster];
        fatInfo_.fat[cluster] = 0;
    }
    writeFAT();
}

DiskImage::DiskImage(DiskData& diskData)
    : impl_ { std::make_unique<impl>(diskData) }
{
}

DiskImage::~DiskImage() = default;

uint32_t DiskImage::numPartitions() const
{
    return impl_->numPartitions();
}

void DiskImage::setPartition(uint32_t index)
{
    impl_->setPartition(index);
}

std::vector<DirectoryEntry> DiskImage::dirEntries(std::string_view path)
{
    return impl_->dirEntries(path);
}

std::vector<uint8_t> DiskImage::readFile(std::string_view path)
{
    return impl_->readFile(path);
}

void DiskImage::writeFile(std::string_view path, const void* data, uint32_t size)
{
    impl_->writeFile(path, data, size);
}

void DiskImage::makeDir(std::string_view path)
{
    impl_->makeDir(path);
}

static uint8_t CalcSectorsPerCluster(const DiskFormat& fmt)
{
    const auto kb = fmt.sizeInBytes() >> 10;
    if (kb > 3*1024)
        ERROR("TODO: CalcSectorsPerCluster for size {} KB", kb);
    switch (kb) {
    case 320:
    case 360:
    case 720:
    case 2880:
        return 2;
    default:
        return 1;
    }
}

static uint16_t CalcRootSectorEntries(const DiskFormat& fmt)
{
    const auto kb = fmt.sizeInBytes() >> 10;
    if (kb < 320)
        return 64; // 4 sectors
    if (kb < 1440)
        return 112; // 7 sectors
    if (kb < 2880)
        return 224; // 14 sectors
    if (kb <= 3*1024)
        return 240; // 15 sectors
    return 512; // 32 sectors
}

static const uint8_t bootsector[] = {
    0xea, 0x43, 0x7c, 0x00, 0x00, // jmp 0:start
    0x31, 0xc0, // xor ax,ax
    0x8e, 0xd8, // mov ds,ax
    0xbe, 0x5e, 0x7c, // mov si,message
    0xac, // lodsb
    0x84, 0xc0, // test al,al
    0x74, 0x09, // jz .done
    // .loop:
    0xbb, 0x07, 0x00, // mov bx,7
    0xb4, 0x0e, // mov ah,0x0e
    0xcd, 0x10, // int 0x10
    0xeb, 0xf2, // jmp .loop
    // .done:
    0x31, 0xc0, // xor ax,ax
    0xcd, 0x16, // int 0x16
    0xcd, 0x19, // int 0x19
    // message:
    0x4e, 0x6f, 0x6e, 0x2d, 0x62, 0x6f, 0x6f, 0x74, 0x61, 0x62, 0x6c, 0x65,
    0x20, 0x64, 0x69, 0x73, 0x6b, 0x2c, 0x20, 0x70, 0x72, 0x65, 0x73, 0x73,
    0x20, 0x61, 0x6e, 0x79, 0x20, 0x6b, 0x65, 0x79, 0x20, 0x74, 0x6f, 0x20,
    0x72, 0x65, 0x62, 0x6f, 0x6f, 0x74, 0x2e, 0x2e, 0x2e, 0x00
};

void FormatDosDisk(DiskData& data)
{
    std::vector<uint8_t> buf(bytesPerSector);
    const auto& fmt = data.format();
    const bool fat16 = false; // FIXME

    if (fmt.totalSectors() > 5760)
        ERROR("TODO: Support totalSectors={} in FormatDosDisk", fmt.totalSectors());

    const uint8_t sectorsPerCluster = CalcSectorsPerCluster(fmt);
    const uint8_t numFats = 2;
    const uint32_t numClusters = fmt.totalSectors() / sectorsPerCluster;
    if (numClusters > 4094)
        ERROR("Too many clusters for FAT12 ({})", numClusters);
    const uint32_t fatBytes = fat16 ? numClusters * 2 : numClusters * 3 / 2;
    const uint16_t sectorsPerFat = static_cast<uint16_t>((fatBytes + bytesPerSector - 1) / bytesPerSector);
    const auto rootSectorEntries = CalcRootSectorEntries(fmt);
    const auto mediaDescriptor = MediaDescriptorFromFormat(fmt);
    const uint16_t reservedSectors = 1; // FIXME (for HDs)
    const uint8_t bootSectOfs = BPB_OFS_EBPB_END;

    const char volumeLabel[12] = "NO NAME    ";

    buf[0x000] = 0xEB; // JMP rel8
    buf[0x001] = bootSectOfs - 2;
    buf[0x002] = 0x90; // NOP
    std::memcpy(&buf[BPB_OFS_OEMNAME], "MSDOS5.0", 8);
    PutU16(&buf[BPB_OFS_BYTES_PER_SEC], bytesPerSector);
    buf[BPB_OFS_SECS_PER_CLUSTER] = sectorsPerCluster;
    PutU16(&buf[BPB_OFS_RESERVED_SECTORS], reservedSectors); 
    buf[BPB_OFS_NUM_FATS] = numFats;
    PutU16(&buf[BPB_OFS_MAX_ROOT_ENTRIES], rootSectorEntries);
    if (fmt.totalSectors() <= 0xffff) {
        PutU16(&buf[BPB_OFS_TOTAL_SECTORS], static_cast<uint16_t>(fmt.totalSectors()));
        PutU32(&buf[BPB_OFS_TOTAL_SECTORS_LARGE], 0);
    } else {
        PutU16(&buf[BPB_OFS_TOTAL_SECTORS], 0);
        PutU32(&buf[BPB_OFS_TOTAL_SECTORS_LARGE], static_cast<uint32_t>(fmt.totalSectors()));
    }
    buf[BPB_OFS_MEDIA_DESCRIPTOR] = mediaDescriptor;
    PutU16(&buf[BPB_OFS_SECTORS_PER_FAT], sectorsPerFat);
    PutU16(&buf[BPB_OFS_SECS_PER_TRACK], static_cast<uint16_t>(fmt.sectorsPerTrack));
    PutU16(&buf[BPB_OFS_NUM_HEADS], static_cast<uint16_t>(fmt.headsPerCylinder));
    PutU32(&buf[BPB_OFS_HIDDEN_SECTORS], 0);
    buf[BPB_OFS_DRIVENUM] = fmt.totalSectors() > 3*1024*1024/512 ? 0x80 : 0x00;
    buf[BPB_OFS_BOOTSIG] = 0x29; // Indicates following three entries are present
    PutU32(&buf[BPB_OFS_SERIAL_NO], static_cast<uint32_t>(CurrentEpochLocalTime() & UINT32_MAX));
    std::memcpy(&buf[BPB_OFS_VOLUME_ID], volumeLabel, 11);
    std::memcpy(&buf[BPB_OFS_FS_TYPE], fat16 ? "FAT16   ": "FAT12   ", 8);

    std::memcpy(&buf[bootSectOfs], bootsector, sizeof(bootsector));
    PutU16(&buf[BPB_OFS_MAGIC], BOOT_MAGIC);

    data.write(&buf[0], 0, bytesPerSector);

    // FAT
    std::vector<uint8_t> tmpBuf(fatBytes);
    tmpBuf[0] = mediaDescriptor;
    tmpBuf[1] = 0xFF;
    tmpBuf[2] = 0xFF;
    if (fat16)
        tmpBuf[3] = 0xFF;

    for (int i = 0; i < numFats; ++i)
        data.write(&tmpBuf[0], (reservedSectors + sectorsPerFat * i) * bytesPerSector, tmpBuf.size());

    // Root directory
    tmpBuf.clear();
    tmpBuf.resize(rootSectorEntries * sizeof(FATDirEntry));
    std::memcpy(&tmpBuf[0], volumeLabel, 11); // name + extension
    auto& de = *reinterpret_cast<FATDirEntry*>(&tmpBuf[0]);
    de.attributes = DE_ATTR_MASK_VOLUME_LABEL;
    SetModifyTime(de, CurrentEpochLocalTime());

    data.write(&tmpBuf[0], (reservedSectors + sectorsPerFat * numFats) * bytesPerSector, tmpBuf.size());
}