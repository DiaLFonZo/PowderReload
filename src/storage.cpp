#include <LittleFS.h>
#include <vector>
#include "storage.h"

uint32_t clockNow() {
    const time_t now = time(nullptr);
    return now > 1700000000 ? static_cast<uint32_t>(now) : 0;
}

namespace Recipes {
namespace {
constexpr const char* RecipesPath = "/recipes.json";
constexpr const char* TempPath = "/recipes.tmp";
std::vector<Recipe> list;
int current = 0;
uint32_t changes = 1;

int tenths(JsonVariantConst value, int low, int high) {
    return constrain(static_cast<int>(lround((value | 0.0) * 10.0)), low, high);
}

bool write() {
    ++changes;
    JsonDocument doc;
    doc["active"] = list.empty() ? 0 : list[current].id;
    JsonArray array = doc["recipes"].to<JsonArray>();
    for (const auto& recipe : list) toJson(array.add<JsonObject>(), recipe);
    File file = LittleFS.open(TempPath, "w");
    if (!file) return false;
    const bool ok = serializeJson(doc, file) > 0;
    file.close();
    // Write then rename, so a power cut never leaves a half-written file.
    if (!ok) return false;
    if (LittleFS.exists(RecipesPath)) LittleFS.remove(RecipesPath);
    return LittleFS.rename(TempPath, RecipesPath);
}
}

bool begin(const Recipe& fallback) {
    if (!LittleFS.begin(true)) return false;
    if (File file = LittleFS.exists(RecipesPath) ? LittleFS.open(RecipesPath, "r") : File()) {
        JsonDocument doc;
        if (!deserializeJson(doc, file)) {
            for (JsonObjectConst item : doc["recipes"].as<JsonArrayConst>()) {
                Recipe recipe;
                fromJson(item, recipe);
                recipe.id = item["id"] | 0;
                if (recipe.id) list.push_back(recipe);
            }
            current = max(0, indexOf(doc["active"] | 0));
        }
    }
    if (list.empty()) {
        Recipe first = fallback;
        first.id = 1;
        first.updated = clockNow();
        list.push_back(first);
        current = 0;
        return write();
    }
    return true;
}

int count() { return list.size(); }
const Recipe& at(int index) { return list[constrain(index, 0, count() - 1)]; }
int activeIndex() { return current; }
const Recipe& active() { return list[current]; }
uint32_t revision() { return changes; }

int indexOf(uint32_t id) {
    for (int i = 0; i < count(); ++i) {
        if (list[i].id == id) return i;
    }
    return -1;
}

void select(int index) {
    if (index < 0 || index >= count() || index == current) return;
    current = index;
    write();
}

uint32_t upsert(const Recipe& recipe) {
    Recipe copy = recipe;
    copy.updated = clockNow() ? clockNow() : recipe.updated;
    const int index = indexOf(recipe.id);
    if (recipe.id && index >= 0) {
        list[index] = copy;
    } else {
        uint32_t next = 1;
        for (const auto& item : list) next = max(next, item.id + 1);
        copy.id = next;
        list.push_back(copy);
    }
    return write() ? copy.id : 0;
}

bool remove(uint32_t id) {
    const int index = indexOf(id);
    if (index < 0 || count() <= 1) return false;  // always keep one recipe
    const uint32_t activeId = active().id;
    list.erase(list.begin() + index);
    current = max(0, indexOf(activeId));
    return write();
}

void updateActive(int charge, int fastRpm, int slowRpm, int slowAt) {
    Recipe& recipe = list[current];
    recipe.charge = charge;
    recipe.fastRpm = fastRpm;
    recipe.slowRpm = slowRpm;
    recipe.slowAt = slowAt;
    if (clockNow()) recipe.updated = clockNow();
    write();
}

void toJson(JsonObject out, const Recipe& recipe) {
    out["id"] = recipe.id;
    out["name"] = recipe.name;
    out["caliber"] = recipe.caliber;
    out["bullet"] = recipe.bullet;
    out["bulletWeight"] = recipe.bulletWeight / 10.0;
    out["powder"] = recipe.powder;
    out["charge"] = recipe.charge / 10.0;
    out["primer"] = recipe.primer;
    out["brass"] = recipe.brass;
    out["oal"] = recipe.oal;
    out["cbto"] = recipe.cbto;
    out["notes"] = recipe.notes;
    out["fastRpm"] = recipe.fastRpm;
    out["slowRpm"] = recipe.slowRpm;
    out["slowAt"] = recipe.slowAt / 10.0;
    out["updated"] = recipe.updated;
}

void fromJson(JsonObjectConst in, Recipe& recipe) {
    recipe.name = in["name"] | "";
    recipe.name.trim();
    if (recipe.name.isEmpty()) recipe.name = "Recipe";
    recipe.caliber = in["caliber"] | "";
    recipe.bullet = in["bullet"] | "";
    recipe.bulletWeight = tenths(in["bulletWeight"], 0, 9999);
    recipe.powder = in["powder"] | "";
    recipe.charge = tenths(in["charge"], 0, 999);
    recipe.primer = in["primer"] | "";
    recipe.brass = in["brass"] | "";
    recipe.oal = in["oal"] | "";
    recipe.cbto = in["cbto"] | "";
    recipe.notes = in["notes"] | "";
    recipe.fastRpm = constrain(in["fastRpm"] | 120, 1, 225);
    recipe.slowRpm = constrain(in["slowRpm"] | 15, 1, 225);
    recipe.slowAt = tenths(in["slowAt"], 1, 200);
    recipe.updated = in["updated"] | 0;
}
}

namespace FillLog {
namespace {
constexpr size_t MaxBytes = 80 * 1024;  // about 1500 fills; the file system is 190 KB

// Drop the oldest half when the log gets too big.
void trim() {
    if (!LittleFS.exists(Path)) return;
    File file = LittleFS.open(Path, "r");
    if (!file || file.size() < MaxBytes) return;
    file.seek(file.size() / 2);
    file.readStringUntil('\n');
    File out = LittleFS.open("/log.tmp", "w");
    uint8_t buffer[512];
    while (out && file.available()) out.write(buffer, file.read(buffer, sizeof(buffer)));
    file.close();
    out.close();
    LittleFS.remove(Path);
    LittleFS.rename("/log.tmp", Path);
}
}

void append(const String& recipe, int target, int actual, const char* result) {
    File file = LittleFS.open(Path, "a");
    if (!file) return;
    String name = recipe;
    name.replace("\"", "\"\"");
    file.printf("%lu,%d.%d,%d.%d,%s,\"%s\"\n", static_cast<unsigned long>(clockNow()), target / 10, target % 10,
                actual / 10, actual % 10, result, name.c_str());
    file.close();
    trim();
}

void clear() {
    if (LittleFS.exists(Path)) LittleFS.remove(Path);
}
}
