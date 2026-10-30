/***************************************************************************
    XML Configuration File Handling.

    Load & Save Hi-Scores.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

// see: http://www.boost.org/doc/libs/1_52_0/doc/html/boost_propertytree/tutorial.html
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <libretro.h>

#include "main.hpp"
#include "config.hpp"
#include "globals.hpp"
#ifdef __LIBRETRO__
#include "lr_setup.hpp"
#else
#include "setup.hpp"
#endif
#include "../utils.hpp"

#include "engine/ohiscore.hpp"
#include "engine/audio/osoundint.hpp"

extern retro_log_printf_t                 log_cb;

#ifdef __PS3__
#define remove std::remove
#endif


// api change in boost 1.56
#if (BOOST_VERSION >= 105600)
typedef boost::property_tree::xml_writer_settings<std::string> xml_writer_settings;
#else
typedef boost::property_tree::xml_writer_settings<char> xml_writer_settings;
#endif

Config config;

Config::Config(void)
{

}


Config::~Config(void)
{
}

void Config::init()
{

}

using boost::property_tree::ptree;
ptree pt_config;

void Config::load_scores(const std::string &filename)
{
    // Create empty property tree object
    ptree pt;

    try
    {
        read_xml(engine.jap ? filename + "_jap.xml" : filename + ".xml" , pt, boost::property_tree::xml_parser::trim_whitespace);
    }
    catch (std::exception &e)
    {
        e.what();
        return;
    }
    
    // Game Scores
    for (int i = 0; i < ohiscore.NO_SCORES; i++)
    {
        score_entry* e = &ohiscore.scores[i];
        
        std::string xmltag = "score";
        xmltag += Utils::to_string(i);  
    
        e->score    = Utils::from_hex_string(pt.get<std::string>(xmltag + ".score",    "0"));
        e->initial1 = pt.get(xmltag + ".initial1", ".")[0];
        e->initial2 = pt.get(xmltag + ".initial2", ".")[0];
        e->initial3 = pt.get(xmltag + ".initial3", ".")[0];
        e->maptiles = Utils::from_hex_string(pt.get<std::string>(xmltag + ".maptiles", "20202020"));
        e->time     = Utils::from_hex_string(pt.get<std::string>(xmltag + ".time"    , "0")); 

        if (e->initial1 == '.') e->initial1 = 0x20;
        if (e->initial2 == '.') e->initial2 = 0x20;
        if (e->initial3 == '.') e->initial3 = 0x20;
    }
}

void Config::save_scores(const std::string &filename)
{
    // Create empty property tree object
    ptree pt;
        
    for (int i = 0; i < ohiscore.NO_SCORES; i++)
    {
        score_entry* e = &ohiscore.scores[i];
    
        std::string xmltag = "score";
        xmltag += Utils::to_string(i);    
        
        pt.put(xmltag + ".score",    Utils::to_hex_string(e->score));
        pt.put(xmltag + ".initial1", e->initial1 == 0x20 ? "." : Utils::to_string((char) e->initial1)); // use . to represent space
        pt.put(xmltag + ".initial2", e->initial2 == 0x20 ? "." : Utils::to_string((char) e->initial2));
        pt.put(xmltag + ".initial3", e->initial3 == 0x20 ? "." : Utils::to_string((char) e->initial3));
        pt.put(xmltag + ".maptiles", Utils::to_hex_string(e->maptiles));
        pt.put(xmltag + ".time",     Utils::to_hex_string(e->time));
    }
    
    try
    {
        write_xml(engine.jap ? filename + "_jap.xml" : filename + ".xml", pt, std::locale(), xml_writer_settings('\t', 1)); // Tab space 1
    }
    catch (std::exception &e)
    {
        log_cb(RETRO_LOG_ERROR, "Error saving hiscores: %s\n", e.what());
    }
}

void Config::load_tiletrial_scores()
{
    const std::string filename = FILENAME_TTRIAL;

    // Counter value that represents 1m 15s 0ms
    static const uint16_t COUNTER_1M_15 = 0x11D0;

    // Create empty property tree object
    ptree pt;

    try
    {
        read_xml(engine.jap ? filename + "_jap.xml" : filename + ".xml" , pt, boost::property_tree::xml_parser::trim_whitespace);
    }
    catch (std::exception &e)
    {
        for (int i = 0; i < 15; i++)
            ttrial.best_times[i] = COUNTER_1M_15;

        e.what();
        return;
    }

    // Time Trial Scores
    for (int i = 0; i < 15; i++)
    {
        ttrial.best_times[i] = pt.get("time_trial.score" + Utils::to_string(i), COUNTER_1M_15);
    }
}

void Config::save_tiletrial_scores()
{
    const std::string filename = FILENAME_TTRIAL;

    // Create empty property tree object
    ptree pt;

    // Time Trial Scores
    for (int i = 0; i < 15; i++)
    {
        pt.put("time_trial.score" + Utils::to_string(i), ttrial.best_times[i]);
    }

    try
    {
        write_xml(engine.jap ? filename + "_jap.xml" : filename + ".xml", pt, std::locale(), xml_writer_settings('\t', 1)); // Tab space 1
    }
    catch (std::exception &e)
    {
        log_cb(RETRO_LOG_ERROR, "Error saving hiscores: %s\n", e.what());
    }
}

bool Config::clear_scores()
{
    // Init Default Hiscores
    ohiscore.init_def_scores();

    bool files_removed = false;

    // Remove XML files if they exist
    // remove() returns 0 on success
    if (!remove(std::string(FILENAME_SCORES).append(".xml").c_str()))
        files_removed = true;
    if (!remove(std::string(FILENAME_SCORES).append("_jap.xml").c_str()))
        files_removed = true;
    if (!remove(std::string(FILENAME_TTRIAL).append(".xml").c_str()))
        files_removed = true;
    if (!remove(std::string(FILENAME_TTRIAL).append("_jap.xml").c_str()))
        files_removed = true;
    if (!remove(std::string(FILENAME_CONT).append(".xml").c_str()))
        files_removed = true;
    if (!remove(std::string(FILENAME_CONT).append("_jap.xml").c_str()))
        files_removed = true;

    return files_removed;
}

void Config::set_fps(int fps)
{
    video.fps = fps;
    // Set core FPS to 30fps, 60fps or 120fps
    if (video.fps == 0)
        this->fps = 30;
    else if (video.fps == 3)
        this->fps = 120;
    else
        this->fps = 60;
    
    // Original game ticks sprites at 30fps but background scroll at 60fps
    if (video.fps == 3)
        tick_fps = 120;
    else if (video.fps < 2)
        tick_fps = 30;
    else
        tick_fps = 60;

    #ifdef COMPILE_SOUND_CODE
    if (config.sound.enabled)
        cannonball::audio.stop_audio();
    osoundint.init();
    if (config.sound.enabled)
        cannonball::audio.start_audio();
    #endif
}
