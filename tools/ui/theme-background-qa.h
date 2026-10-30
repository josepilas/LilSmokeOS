// Included by the deterministic software-renderer fixture.
static void backgroundTests(Renderer &r, Font &font, const fs::path &out) {
    using themeui::ExtraLayer;
    const auto pattern = (out / "media-a.png").string();
    auto image = [&](const char *name, const char *extra) {
        ThemeElement e;
        e.type = "image"; e.name = name;
        e.props = {{"path", pattern}, {"pos", "0 0"}, {"size", "1 1"},
                   {"extra", extra}, {"zindex", "0"}};
        return e;
    };
    const themeui::Rect full{0, 0, r.width(), r.height()};
    auto composite = [&](const ExtraLayer *layers, size_t count) {
        r.clear(0x000000FF);
        themeui::drawSystemExtras(r, font, layers, count, -1000, 1000, 10);
        return pixels(r);
    };
    ThemeView page;
    page.elements.push_back(image("background", "static"));
    page.elements[0].props["color"] = "FFFFFF80";
    ExtraLayer reference;
    reference.view = &page; reference.clip = full;
    for (bool vertical : {false, true}) {
        const int extent = vertical ? r.height() : r.width();
        for (int split : {1, extent / 3, extent / 2, extent - 1}) {
            ExtraLayer layers[2]{reference, reference};
            layers[0].clip = vertical ? themeui::Rect{0, 0, r.width(), split}
                                      : themeui::Rect{0, 0, split, r.height()};
            layers[1].clip = vertical ? themeui::Rect{0, split, r.width(), extent - split}
                                      : themeui::Rect{split, 0, extent - split, r.height()};
            // SDL's clipped source rounding is part of the reference raster.
            // Verify static transform semantics without comparing two different clips.
            const auto anchored = composite(layers, 2);
            layers[0].offsetX = vertical ? 0 : split - extent;
            layers[0].offsetY = vertical ? split - extent : 0;
            layers[1].offsetX = vertical ? 0 : split;
            layers[1].offsetY = vertical ? split : 0;
            const auto clipped = composite(layers, 2);
            if (clipped != anchored) {
                size_t count = 0, first = 0;
                for (size_t p = 0; p < clipped.size(); ++p)
                    if (clipped[p] != anchored[p]) { if (!count) first = p; ++count; }
                std::printf("BACKGROUND_DIFF vertical=%d split=%d pixels=%zu first=%zu expected=%08x actual=%08x\n",
                    vertical, split, count, first, anchored[first], clipped[first]);
                savePixels(r, out / "background-clipped-failure.png");
            }
            require(clipped == anchored,
                    "static page background shifted, overlapped or left a seam");
        }
    }
    page.elements[0] = image("staticBackground", "false");
    page.elements[0].props["color"] = "FFFFFF80";
    reference.fixedBackgrounds = true;
    const auto fixed = composite(&reference, 1);
    ExtraLayer globalAndPage[2]{reference, reference};
    globalAndPage[1].fixedBackgrounds = false;
    globalAndPage[1].offsetX = r.width() / 3;
    require(composite(globalAndPage, 2) == fixed,
            "global staticBackground was omitted or drawn twice");
    page.elements[0] = image("background", "true");
    page.elements[0].props["color"] = "FFFFFF80";
    std::vector<const ThemeElement *> shared{&page.elements[0]};
    reference.stationary = &shared;
    const auto sharedOnce = composite(&reference, 1);
    ExtraLayer sharedLayers[3]{reference, reference, reference};
    sharedLayers[1].fixedBackgrounds = sharedLayers[2].fixedBackgrounds = false;
    sharedLayers[1].clip = {0, 0, r.width() / 3, r.height()};
    sharedLayers[2].clip = {r.width() / 3, 0, r.width() - r.width() / 3, r.height()};
    sharedLayers[1].offsetX = -r.width() * 2 / 3;
    sharedLayers[2].offsetX = r.width() / 3;
    require(composite(sharedLayers, 3) == sharedOnce,
            "shared background clipped, shifted or blended more than once");
    reference.stationary = nullptr;

    // Moving artwork must still move, and explicit static extras keep their tracks.
    reference.fixedBackgrounds = false;
    page.elements[0] = image("foreground", "true");
    const auto origin = composite(&reference, 1);
    reference.offsetX = r.width() / 4;
    require(composite(&reference, 1) != origin, "ordinary artwork no longer moves");
    page.elements[0].props["extra"] = "static";
    ThemeAnimation track;
    track.property = ThemeAnimation::Property::X;
    track.from = 0; track.to = 0.25f;
    track.hasFrom = track.hasTo = true; track.durationMs = 500;
    page.elements[0].animations.push_back(track);
    reference.elapsed = 0;
    const auto animationStart = composite(&reference, 1);
    reference.elapsed = 250;
    require(composite(&reference, 1) != animationStart,
            "static extra lost its authored animation");

    // Exercise the real SystemView's shared-image recognition, including ./../ aliases.
    const fs::path root = out / "fixtures" / "stationary-scenery";
    fs::create_directories(root);
    makeImage(root / "background.png", 0xFF2488CC, true);
    for (const std::string type : {"horizontal", "vertical"}) {
        for (const char *id : {"snes", "nes", "gba", "md"}) {
            fs::create_directories(root / type / id);
            std::ofstream xml(root / type / id / "theme.xml");
            xml << "<theme><formatVersion>4</formatVersion><view name=\"system\">"
                "<image name=\"background\" extra=\"true\"><path>"
                << (std::string(id) == "snes" ? "../../background.png" : "./../../background.png")
                << "</path><pos>0 0</pos><size>1 1</size><zIndex>0</zIndex></image>"
                "<carousel name=\"systemcarousel\"><type>" << type << "</type>"
                "<pos>0 0.55</pos><size>1 0.2</size><color>00000000</color>"
                "<logoSize>0.15 0.1</logoSize></carousel>"
                "<helpsystem name=\"help\"><visible>false</visible></helpsystem>"
                "</view></theme>";
        }
        for (const char *style : {"slide", "fade", "instant"}) {
            std::string dir = (root / type).string();
            SystemView view(systems(), {}, &dir);
            Theme theme;
            setenv("LILSMOKE_THEME_TRANSITION", style, 1);
            qaNow = 60000; view.onEnter();
            auto render = [&]() {
                r.clear(0x000000FF); view.render(r, font, theme); return pixels(r);
            };
            const auto before = render();
            const auto checkScenery = [&]() {
                const auto current = render();
                for (int y = r.height() / 12; y < r.height() / 3; ++y)
                    for (int x = 0; x < r.width(); ++x)
                        require(current[y * r.width() + x] == before[y * r.width() + x],
                                "shared SystemView background moved during navigation");
            };
            qaNow += 1000; press(view, InputButton::Right);
            for (unsigned t : {0u, 40u, 100u, 250u, 499u, 500u}) {
                qaNow = 61000 + t; checkScenery();
            }
            for (int i = 0; i < 23; ++i) {
                qaNow += 17; press(view, i % 3 ? InputButton::Right : InputButton::Left);
                checkScenery();
            }
            qaNow += 1000; checkScenery();
        }
    }
    std::puts("THEME_BACKGROUND_OK static=8 global_once=1 moving_art=1 authored_tracks=1 shared_alias=1 horizontal_vertical=1 rapid_wrap=1 slide_fade_instant=1");
}
