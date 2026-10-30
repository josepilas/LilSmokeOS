#include "Theme.h"
#include "ThemeAnimation.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <string>

namespace {
bool countAllocations = false;
size_t allocations = 0;
unsigned checks = 0;

void check(bool pass, const char *message) {
    ++checks;
    if (!pass) {
        countAllocations = false;
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void near(float actual, float expected, const char *message) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.0001f) {
        countAllocations = false;
        std::fprintf(stderr, "FAIL: %s: got %.8g, expected %.8g\n", message, actual, expected);
        std::exit(1);
    }
    ++checks;
}

std::string writeXml(const std::string &dir, const char *name, const std::string &xml) {
    const std::string path = dir + "/" + name;
    std::ofstream file(path);
    file << xml;
    check(file.good(), "write isolated fixture");
    return path;
}

const lilsmoke::ThemeElement &element(const lilsmoke::Theme &theme, const char *name,
                                    const char *view = "system", const char *type = "image") {
    const auto *foundView = theme.findView(view);
    check(foundView != nullptr, "fixture view exists");
    const auto *found = foundView->find(type, name);
    check(found != nullptr, "fixture element exists");
    return *found;
}

using lilsmoke::Theme;
using lilsmoke::ThemeAnimation;
using lilsmoke::ThemeElement;
using lilsmoke::themeui::sampleAnimation;

void testGrammar(const std::string &dir) {
    const std::string path = writeXml(dir, "grammar.xml", R"XML(
<?xml version="1.0"?>
<theme><formatVersion>7</formatVersion><view name="system, grid">
  <image name="logo, other" extra="true">
    <pos>0.2 0.75</pos><scale>1</scale><opacity>0.8</opacity><rotation>10</rotation>
    <storyboard>
      <animation property="x" from="0.1" to="0.9" begin="100" duration="400" mode="easeIn"/>
      <animation property="y" from="0.05" begin="0" duration="350" mode="easeOut"/>
      <animation property="opacity" from="0" to="1" duration="350" mode="linear"/>
      <animation property="scale" from="0" to="1" duration="350" mode="easeOut"/>
      <animation property="rotation" to="90" duration="350"/>
    </storyboard>
    <storyboard event="activate"><animation property="x" to="${target}"/></storyboard>
    <storyboard event="deactivate"><animation property="opacity" from="1" to="0" duration="100"/></storyboard>
    <text><![CDATA[kept <literal> text]]></text>
  </image>
</view><variables><target>0.95</target></variables></theme>)XML");
    Theme theme;
    check(theme.load(path), "load actual nested storyboard grammar");
    const auto &logo = element(theme, "logo");
    check(logo.animations.size() == 7, "all scalar tracks retained, including event tracks");
    check(logo.props.find("storyboard") == logo.props.end(), "storyboard not flattened into a string");
    check(logo.get("text") == "kept <literal> text", "CDATA property after storyboard preserved");
    check(logo.getBool("extra"), "element attributes preserved");
    check(element(theme, "other", "grid").animations.size() == 7, "multi-name/view copies retain tracks");

    auto value = sampleAnimation(logo, 175, "", 0.2f, 0.75f);
    near(value.x, 0.128125f, "delayed quadratic easeIn");
    near(value.y, 0.575f, "omitted to uses base y with upstream quadratic easeOut");
    near(value.scale, 0.75f, "scale is an absolute factor");
    near(value.opacity, 0.5f, "linear opacity");
    near(value.rotation, 50.0f, "omitted from uses static rotation");
    check(value.active, "storyboard active until longest track finishes");
    value = sampleAnimation(logo, 500, "", 0.2f, 0.75f);
    near(value.x, 0.9f, "terminal x held");
    near(value.y, 0.75f, "terminal omitted endpoint is base");
    check(!value.active, "completed storyboard is inactive");
    value = sampleAnimation(logo, 50, "activate", 0.2f, 0.75f);
    near(value.x, 0.95f, "event and substituted endpoint retained");
    near(value.opacity, 0.8f, "named storyboard replaces, not layers with, default");
    check(!value.active, "zero-duration assignment finishes immediately");
    value = sampleAnimation(logo, 50, "deactivate", 0.2f, 0.75f);
    near(value.opacity, 0.5f, "deactivate is independent");
    near(value.x, 0.2f, "deactivate cannot sample activate track");
    value = sampleAnimation(logo, 175, "unknown", 0.2f, 0.75f);
    near(value.y, 0.575f, "unmatched event falls back to default storyboard");
}

void testTiming(const std::string &dir) {
    Theme theme;
    check(theme.load(writeXml(dir, "timing.xml", R"XML(<theme><view name="system">
      <image name="repeat"><storyboard repeat="3">
        <animation property="x" from="0" to="1" begin="10" duration="100" repeat="2"/>
        <animation property="rotation" from="0" to="40" duration="400"/>
      </storyboard></image>
      <image name="reverse"><storyboard><animation property="scale" from="0" to="2"
        duration="100" repeat="2" autoReverse="true"/></storyboard></image>
      <image name="instant"><storyboard><animation property="x" from="0.1" to="0.9" begin="100"/></storyboard></image>
      <image name="base"><scale>2</scale><storyboard>
        <animation property="scale" begin="200" duration="100"/>
        <animation property="scale" from="0" to="1" duration="100"/>
      </storyboard></image>
      <image name="order"><storyboard>
        <animation property="x" from="2" to="3" begin="200" duration="100"/>
        <animation property="x" from="0" to="1" duration="100"/>
        <animation property="y" to="0.2"/>
        <animation property="y" to="0.7"/>
      </storyboard></image>
      <image name="ease"><storyboard>
        <animation property="x" from="0" to="1" duration="1000" mode="Linear"/>
        <animation property="y" from="0" to="1" duration="1000" mode="EaseIn"/>
        <animation property="scale" from="0" to="1" duration="1000" mode="EaseOut"/>
        <animation property="opacity" from="0" to="1" duration="1000" mode="EaseInOut"/>
      </storyboard></image>
    </view></theme>)XML")), "load timing fixtures");
    const auto &repeat = element(theme, "repeat");
    near(sampleAnimation(repeat, 9, "", 0.25f, 0).x, 0.25f, "delay preserves base");
    near(sampleAnimation(repeat, 10, "", 0.25f, 0).x, 0, "track starts at begin");
    near(sampleAnimation(repeat, 60, "", 0.25f, 0).x, 0.5f, "first iteration midpoint");
    near(sampleAnimation(repeat, 110, "", 0.25f, 0).x, 0, "repeat 2 means two total iterations");
    near(sampleAnimation(repeat, 160, "", 0.25f, 0).x, 0.5f, "second iteration midpoint");
    near(sampleAnimation(repeat, 210, "", 0.25f, 0).x, 1, "track holds endpoint while board runs");
    near(sampleAnimation(repeat, 400, "", 0.25f, 0).x, 0.25f, "storyboard repeats entire shared timeline");
    near(sampleAnimation(repeat, 460, "", 0.25f, 0).x, 0.5f, "begin occurs only once per storyboard iteration");
    check(sampleAnimation(repeat, 1199, "", 0, 0).active, "last finite storyboard iteration active");
    const auto terminal = sampleAnimation(repeat, 1200, "", 0, 0);
    check(!terminal.active, "finite storyboard repeat stops");
    near(terminal.x, 1, "finite repeat holds to");
    near(terminal.rotation, 40, "whole storyboard final endpoint");
    near(sampleAnimation(repeat, UINT32_MAX, "", 0, 0).x, 1, "large timestamp does not wrap timeline");
    check(!sampleAnimation(repeat, UINT32_MAX, "", 0, 0).active, "UINT32_MAX is completed");

    const auto &reverse = element(theme, "reverse");
    near(sampleAnimation(reverse, 100, "", 0, 0).scale, 2, "autoReverse reaches to at turnaround");
    near(sampleAnimation(reverse, 150, "", 0, 0).scale, 1, "autoReverse midpoint");
    near(sampleAnimation(reverse, 200, "", 0, 0).scale, 0, "one reverse iteration includes both legs");
    near(sampleAnimation(reverse, 300, "", 0, 0).scale, 2, "second reverse iteration turnaround");
    near(sampleAnimation(reverse, 400, "", 0, 0).scale, 0, "reverse completion holds from");
    check(!sampleAnimation(reverse, 400, "", 0, 0).active, "reverse repeat is bounded");

    const auto &instant = element(theme, "instant");
    near(sampleAnimation(instant, 99, "", 0.25f, 0).x, 0.25f, "zero duration before begin");
    check(sampleAnimation(instant, 99, "", 0, 0).active, "pending zero duration is active");
    near(sampleAnimation(instant, 100, "", 0.25f, 0).x, 0.9f, "zero duration assigns to at begin");
    check(!sampleAnimation(instant, 100, "", 0, 0).active, "zero duration completes at begin");
    near(sampleAnimation(element(theme, "base"), 250, "", 0, 0).scale, 2, "both omitted endpoints use static base, not a prior track");
    near(sampleAnimation(element(theme, "order"), 250, "", 0, 0).x, 2.5f, "latest begin wins independent of XML order");
    near(sampleAnimation(element(theme, "order"), 250, "", 0, 0).y, 0.7f, "equal begin resolves by later XML order");
    const auto &ease = element(theme, "ease");
    auto values = sampleAnimation(ease, 250, "", 0, 0);
    near(values.x, 0.25f, "linear quarter");
    near(values.y, 0.0625f, "quadratic easeIn quarter");
    near(values.scale, 0.4375f, "quadratic easeOut quarter");
    near(values.opacity, 0.125f, "quadratic easeInOut first half");
    near(sampleAnimation(ease, 750, "", 0, 0).opacity, 0.875f, "quadratic easeInOut second half");
    const auto forward = sampleAnimation(ease, 750, "", 0, 0);
    (void)sampleAnimation(ease, 10, "", 0, 0);
    near(sampleAnimation(ease, 750, "", 0, 0).y, forward.y, "sampling order is deterministic and stateless");
}

void testMerging(const std::string &dir) {
    writeXml(dir, "included.xml", R"XML(<theme><view name="system">
      <image name="keep"><scale>2</scale><storyboard><animation property="y" from="0" duration="200"/></storyboard>
        <storyboard event="activate"><animation property="opacity" from="0" duration="100"/></storyboard>
        <storyboard event="deactivate"><animation property="x" to="1"/></storyboard></image>
      <image name="clear"><storyboard><animation property="x" to="1"/></storyboard></image>
      <image name="style"><storyboard><animation property="x" to="0.4" duration="200"/></storyboard></image>
    </view></theme>)XML");
    writeXml(dir, "picked.xml", R"XML(<theme><view name="system"><image name="keep">
      <storyboard event="activate" repeat="2"><animation property="rotation" from="0" to="${turn}" duration="100"/></storyboard>
      <storyboard event="deactivate"/>
    </image></view></theme>)XML");
    writeXml(dir, "not-picked.xml", R"XML(<theme><view name="system"><image name="keep">
      <storyboard><animation property="scale" to="4"/></storyboard>
    </image></view></theme>)XML");
    const auto path = writeXml(dir, "merge.xml", R"XML(<theme>
      <include>included.xml</include>
      <subset name="colorset"><include name="picked">picked.xml</include><include name="ignored">not-picked.xml</include></subset>
      <view name="system">
        <image name="keep"><opacity>0.6</opacity>
          <storyboard event="filtered" ifSubset="colorset:ignored"><animation property="x" to="1"/></storyboard>
          <storyboard event="conditional" ifSubset="colorset:picked">
            <animation property="x" to="0.3" ifSubset="colorset:ignored"/>
            <animation property="y" from="0" to="1" duration="100" ifSubset="colorset:picked"/>
          </storyboard>
        </image>
        <image name="clear"><storyboard/></image>
        <image name="style"><color>ABCDEF</color></image>
      </view>
      <customView name="derived" inherits="system"><image name="keep"><rotation>10</rotation></image></customView>
      <variables><turn>80</turn></variables>
    </theme>)XML");
    setenv("LILSMOKE_THEME_SUBSETS", "colorset=picked", 1);
    Theme theme;
    check(theme.load(path), "load includes and selected subset");
    unsetenv("LILSMOKE_THEME_SUBSETS");
    const auto &keep = element(theme, "keep");
    check(keep.animations.size() == 3, "merge preserves other events, replaces same event, clears empty event");
    near(keep.getFloat("scale"), 2, "included property preserved");
    near(keep.getFloat("opacity"), 0.6f, "later scalar override preserved");
    near(sampleAnimation(keep, 50, "activate", 0.2f, 0.8f).rotation, 40, "nested from/to/duration attributes survive subset merge");
    near(sampleAnimation(keep, 150, "activate", 0.2f, 0.8f).rotation, 40, "storyboard repeat attribute survives merge");
    near(sampleAnimation(keep, 50, "", 0.2f, 0.8f).y, 0.2f, "default storyboard survives named replacement and style-only merge");
    near(sampleAnimation(keep, 50, "conditional", 0.2f, 0.8f).y, 0.5f, "storyboard and track condition attributes respected");
    near(sampleAnimation(keep, 50, "conditional", 0.2f, 0.8f).x, 0.2f, "filtered track not parsed");
    check(element(theme, "clear").animations.empty(), "self-closing storyboard clears inherited default");
    check(element(theme, "style").animations.size() == 1, "style-only override preserves nested tracks");
    check(element(theme, "style").getColor("color") == 0xABCDEFFF, "other XML properties unaffected");
    check(element(theme, "keep", "derived").animations.size() == 3, "custom-view inheritance preserves tracks");
    check(theme.load(writeXml(dir, "reload.xml", "<theme><view name=\"system\"><image name=\"keep\"/></view></theme>")), "reload succeeds");
    check(element(theme, "keep").animations.empty(), "reload discards all prior tracks");
}

void testBounds(const std::string &dir) {
    std::string xml = R"XML(<theme><view name="system"><image name="bad"><storyboard>
      <animation property="x" from="nan"/>
      <animation property="x" from="inf"/>
      <animation property="x" from="1e9999"/>
      <animation property="x" to="1.2junk"/>
      <animation property="x" begin="-1"/>
      <animation property="x" duration="1.5"/>
      <animation property="x" repeat="0"/>
      <animation property="x" repeat="-1"/>
      <animation property="x" repeat="abc"/>
      <animation property="x" repeat="forever"/>
      <animation property="x" repeat="infinite"/>
      <animation property="x" repeatAt="1"/>
      <animation property="x" autoReverse="maybe"/>
      <animation property="x" mode="easeInQuint"/>
      <animation property="shader.amount" to="1"/>
      <animation property="pos" to="1 1"/>
      <animation property="x" enabled="false"/>
      <animation property="x" enabled="{game:dynamic}"/>
      <animation to="1"/>
      <wrapper><animation property="x" to="1"/></wrapper>
    </storyboard><text>after bad tracks</text></image>
    <image name="infinite"><storyboard repeat="forever"><animation property="x" to="1"/></storyboard></image>
    <image name="repeatAt"><storyboard repeatAt="10"><animation property="x" to="1"/></storyboard></image>
    <image name="empty"><storyboard/></image>
    <image name="clamp"><storyboard repeat="999999999999999999999999999999">
      <animation property="x" from="-1e200" to="1e200" duration="999999999999999999999999999999" begin="999999999999999999" repeat="9999999999999999999999" autoReverse="true"/>
      <animation property="opacity" from="-8" to="9"/>
      <animation property="scale" from="-8" to="99"/>
      <animation property="rotation" from="-1e200" to="1e200"/>
    </storyboard></image><image name="many"><storyboard>)XML";
    for (unsigned i = 0; i < 1000; ++i)
        xml += "<animation property=\"x\" to=\"" + std::to_string(i) + "\" duration=\"100\"/>";
    xml += "</storyboard></image><image name=\"events\">";
    for (unsigned i = 0; i < 100; ++i)
        xml += "<storyboard event=\"" + std::to_string(i) + "\"><animation property=\"x\" to=\"1\"/></storyboard>";
    xml += "</image><image name=\"long\"><storyboard event=\"" + std::string(65, 'e') +
           "\"><animation property=\"x\" to=\"1\"/></storyboard><storyboard><animation property=\"x\" to=\"" +
           std::string(65, '1') + "\"/></storyboard></image></view></theme>";
    Theme theme;
    check(theme.load(writeXml(dir, "bounds.xml", xml)), "load adversarial scalar fixture");
    check(element(theme, "bad").animations.empty(), "unsupported and invalid tracks rejected");
    check(element(theme, "bad").get("text") == "after bad tracks", "bad tracks cannot consume following properties");
    check(element(theme, "infinite").animations.empty(), "infinite storyboard never expanded");
    check(element(theme, "repeatAt").animations.empty(), "repeatAt unsupported, not approximated");
    check(element(theme, "long").animations.empty(), "event and number lengths bounded");
    check(element(theme, "empty").animations.empty(), "empty storyboard safe");
    const auto &clamp = element(theme, "clamp");
    check(clamp.animations.size() == 4, "finite overflow endpoints saturated safely");
    const auto &track = clamp.animations.front();
    check(track.beginMs == 60000 && track.durationMs == 60000, "timing clamps before overflow");
    check(track.repeat == 32 && track.storyboardRepeat == 32, "finite repeat bounded at both levels");
    check(track.storyboardDurationMs == ThemeAnimation::MaxStoryboardMs, "shared storyboard duration includes reverse and finite repeats");
    const auto values = sampleAnimation(clamp, UINT32_MAX, "", 0, 0);
    near(values.x, -16, "reverse terminal x bounded");
    near(values.scale, 4, "scale hard cap protects software renderer");
    near(values.opacity, 1, "opacity hard cap");
    near(values.rotation, 36000, "finite rotation bound");
    check(!values.active, "bounded timeline completed at large timestamp");
    check(element(theme, "many").animations.size() == 32, "track storage bounded at 32, no repeat expansion");
    check(element(theme, "events").animations.size() == 32, "track bound shared across events");
    check(element(theme, "events").storyboardEvents.size() == 32, "empty/event merge metadata also bounded");
}

void testNoAllocation() {
    ThemeElement empty;
    empty.props = {{"scale", "3"}, {"opacity", "0.4"}, {"rotation", "20"}, {"x", "99"}, {"y", "99"}};
    ThemeElement invalid;
    invalid.props = {{"scale", "nan"}, {"opacity", "bad"}, {"rotation", "1e99999999999"}};
    ThemeElement longBase;
    longBase.props = {{"scale", std::string(4096, '9')}, {"opacity", "inf"}, {"rotation", "nope"}};
    ThemeElement animated = empty;
    ThemeAnimation track;
    track.event = "activate";
    track.property = ThemeAnimation::Property::X;
    track.hasFrom = track.hasTo = true;
    track.from = -1;
    track.to = 1;
    track.durationMs = 1000;
    track.storyboardDurationMs = 1000;
    for (unsigned i = 0; i < 40; ++i) animated.animations.push_back(track);
    // Also defend against out-of-contract model values constructed without XML.
    animated.animations[0].property = static_cast<ThemeAnimation::Property>(255);
    animated.animations[1].durationMs = UINT32_MAX;
    animated.animations[1].beginMs = UINT32_MAX;
    animated.animations[1].repeat = UINT32_MAX;
    animated.animations[1].storyboardRepeat = UINT32_MAX;
    animated.animations[1].storyboardDurationMs = UINT32_MAX;
    animated.animations[2].from = std::numeric_limits<float>::infinity();
    animated.animations[2].to = std::numeric_limits<float>::quiet_NaN();
    const std::string event = "activate";
    const std::string absent = "no-matching-event-and-no-temporary-string-allocation";
    allocations = 0;
    countAllocations = true;
    auto values = sampleAnimation(empty, 0, event, 0.2f, 0.7f);
    near(values.x, 0.2f, "base x comes from arguments, not x property");
    near(values.y, 0.7f, "base y comes from arguments, not y property");
    near(values.scale, 3, "base scale");
    near(values.opacity, 0.4f, "base opacity");
    near(values.rotation, 20, "base rotation");
    check(!values.active, "no tracks inactive");
    values = sampleAnimation(invalid, UINT32_MAX, absent,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    near(values.x, 0, "non-finite base x replaced");
    near(values.y, 0, "non-finite base y replaced");
    near(values.scale, 1, "non-finite scale defaults");
    near(values.opacity, 1, "malformed opacity defaults without exceptions");
    near(values.rotation, 0, "overflow rotation defaults without exceptions");
    values = sampleAnimation(longBase, 0, event, 0, 0);
    near(values.scale, 1, "unbounded base numeric string never parsed");
    for (uint32_t i = 0; i < 10000; ++i) {
        values = sampleAnimation(animated, i * 1000u, event, 0.2f, 0.7f);
        check(std::isfinite(values.x) && values.scale <= 4 && values.opacity <= 1,
              "bounded stateless evaluation remains finite");
    }
    countAllocations = false;
    check(allocations == 0, "evaluation performs no heap allocations, even on first call and malformed bases");
}
}  // namespace

// Catch both C++ heap use and direct C allocation in the isolated linked units.
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_calloc(size_t, size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void *__wrap_malloc(size_t n) {
    if (countAllocations) ++allocations;
    return __real_malloc(n);
}
extern "C" void *__wrap_calloc(size_t n, size_t size) {
    if (countAllocations) ++allocations;
    return __real_calloc(n, size);
}
extern "C" void *__wrap_realloc(void *ptr, size_t n) {
    if (countAllocations) ++allocations;
    return __real_realloc(ptr, n);
}
#ifdef THEME_ANIMATION_ASAN
extern "C" void __sanitizer_malloc_hook(const volatile void *, size_t) {
    if (countAllocations) ++allocations;
}
#endif
__attribute__((noinline)) void *operator new(size_t n) {
    if (countAllocations) ++allocations;
    if (void *ptr = std::malloc(n == 0 ? 1 : n)) return ptr;
    throw std::bad_alloc();
}
void *operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline)) void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete[](void *ptr) noexcept { ::operator delete(ptr); }
void operator delete(void *ptr, size_t) noexcept { ::operator delete(ptr); }
void operator delete[](void *ptr, size_t) noexcept { ::operator delete(ptr); }

int main(int argc, char **argv) {
    check(argc == 2, "pass an isolated fixture directory");
    std::filesystem::create_directories(argv[1]);
    testNoAllocation();
    testGrammar(argv[1]);
    testTiming(argv[1]);
    testMerging(argv[1]);
    testBounds(argv[1]);
    std::printf("PASS: %u theme storyboard checks; zero sampling allocations; pointer width %zu\n",
                checks, sizeof(void *) * 8);
    return 0;
}
