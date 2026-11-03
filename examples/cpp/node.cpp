#include "AudioManager.h"
#include "core/types/types.h"
#include "core/window/window.h"
#include "core/css/css.h"
#include "core/animations/animation.h"
#include "core/animations/animator.h"
#include "core/animations/easing.h"
#include "components/button/button.h"
#include "components/view/view.h"
#include "components/image/image.h"
#include "components/textbox/textbox.h"
#include "components/text/text.h"
#include "components/menu/menu.h"
#include "components/dropdown/dropdown.h"
#include "components/sidebar/sidebar.h"
#include "components/menubar/menubar.h"
#include "components/progressbar/progressbar.h"
#include "../window_manager/window_manager.h"

#include <node_api.h>
#include <map>
#include <memory>
#include <uv.h>

namespace GlassUI
{

    static std::map<int, std::shared_ptr<Component>> components;
    static std::map<std::string, Style> styleClasses;
    static std::unique_ptr<Window> appWindow;
    static int nextComponentId = 1;
    static std::map<int, std::vector<napi_ref>> menuCallbacks;
    static std::map<int, std::vector<napi_ref>> dropdownCallbacks;
    static std::map<int, napi_ref> buttonCallbacks;
    static std::unique_ptr<CSSParser> cssParser;
    static std::map<int, std::shared_ptr<AudioFile>> audioFiles;
    static napi_ref updateCallback;
    static napi_env savedEnv = nullptr;
    static int nextAudioId = 1;

    napi_value CreateSidebar(napi_env env, napi_callback_info info)
    {
        auto sidebar = std::make_shared<Sidebar>();
        int id = nextComponentId++;
        components[id] = sidebar;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value AddSidebarItem(napi_env env, napi_callback_info info)
    {
        size_t argc = 5;
        napi_value args[5];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t sidebarId, contentId;
        napi_get_value_int32(env, args[0], &sidebarId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *id_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], id_buf, str_len + 1, &str_len);
        std::string itemId(id_buf);
        delete[] id_buf;

        napi_get_value_string_utf8(env, args[2], nullptr, 0, &str_len);
        char *label_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[2], label_buf, str_len + 1, &str_len);
        std::string label(label_buf);
        delete[] label_buf;

        napi_get_value_int32(env, args[3], &contentId);

        std::string icon = "";
        if (argc > 4)
        {
            napi_valuetype type;
            napi_typeof(env, args[4], &type);
            if (type == napi_string)
            {
                napi_get_value_string_utf8(env, args[4], nullptr, 0, &str_len);
                char *icon_buf = new char[str_len + 1];
                napi_get_value_string_utf8(env, args[4], icon_buf, str_len + 1, &str_len);
                icon = std::string(icon_buf);
                delete[] icon_buf;
            }
        }

        auto sidebarComp = components[sidebarId];
        auto contentComp = components[contentId];

        if (sidebarComp && contentComp)
        {
            Sidebar *sidebar = static_cast<Sidebar *>(sidebarComp.get());
            if (sidebar)
            {
                sidebar->addItem(itemId, label, contentComp, icon);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetSidebarActiveItem(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t sidebarId;
        napi_get_value_int32(env, args[0], &sidebarId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *id_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], id_buf, str_len + 1, &str_len);
        std::string itemId(id_buf);
        delete[] id_buf;

        auto comp = components[sidebarId];
        if (comp)
        {
            Sidebar *sidebar = static_cast<Sidebar *>(comp.get());
            if (sidebar)
            {
                sidebar->setActiveItem(itemId);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value GetSidebarActiveItem(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t sidebarId;
        napi_get_value_int32(env, args[0], &sidebarId);

        std::string activeId = "";
        auto comp = components[sidebarId];
        if (comp)
        {
            Sidebar *sidebar = static_cast<Sidebar *>(comp.get());
            if (sidebar)
            {
                activeId = sidebar->getActiveItem();
            }
        }

        napi_value result;
        napi_create_string_utf8(env, activeId.c_str(), activeId.length(), &result);
        return result;
    }

    napi_value SetSidebarWidth(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t sidebarId;
        double width;
        napi_get_value_int32(env, args[0], &sidebarId);
        napi_get_value_double(env, args[1], &width);

        auto comp = components[sidebarId];
        if (comp)
        {
            Sidebar *sidebar = static_cast<Sidebar *>(comp.get());
            if (sidebar)
            {
                sidebar->setSidebarWidth((float)width);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetSidebarItemIcon(napi_env env, napi_callback_info info)
    {
        size_t argc = 3;
        napi_value args[3];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t sidebarId;
        napi_get_value_int32(env, args[0], &sidebarId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *id_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], id_buf, str_len + 1, &str_len);
        std::string itemId(id_buf);
        delete[] id_buf;

        napi_get_value_string_utf8(env, args[2], nullptr, 0, &str_len);
        char *icon_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[2], icon_buf, str_len + 1, &str_len);
        std::string iconPath(icon_buf);
        delete[] icon_buf;

        auto comp = components[sidebarId];
        if (comp)
        {
            Sidebar *sidebar = static_cast<Sidebar *>(comp.get());
            if (sidebar)
            {
                sidebar->setItemIcon(itemId, iconPath);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetSidebarFont(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t sidebarId;
        napi_get_value_int32(env, args[0], &sidebarId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *font_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], font_buf, str_len + 1, &str_len);
        std::string fontName(font_buf);
        delete[] font_buf;

        auto comp = components[sidebarId];
        if (comp)
        {
            Sidebar *sidebar = static_cast<Sidebar *>(comp.get());
            if (sidebar)
            {
                sidebar->setFont(fontName);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetSidebarItemHeight(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double height;
        napi_get_value_double(env, args[0], &height);

        Sidebar::setGlobalItemHeight((float)height);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetSidebarIconSize(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double size;
        napi_get_value_double(env, args[0], &size);

        Sidebar::setGlobalIconSize((float)size);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetSidebarIconMargin(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double margin;
        napi_get_value_double(env, args[0], &margin);

        Sidebar::setGlobalIconMargin((float)margin);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value CreateGlassUIWindow(napi_env env, napi_callback_info info)
    {
        size_t argc = 3;
        napi_value args[3];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t width, height;
        napi_get_value_int32(env, args[0], &width);
        napi_get_value_int32(env, args[1], &height);

        size_t str_len;
        napi_get_value_string_utf8(env, args[2], nullptr, 0, &str_len);
        char *title_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[2], title_buf, str_len + 1, &str_len);
        std::string title(title_buf);
        delete[] title_buf;

        appWindow = std::make_unique<GlassUIWindow>(width, height, title);
        appWindow->init();

        cssParser = std::make_unique<CSSParser>();

        for (auto &pair : dropdownCallbacks)
        {
            int dropdownId = pair.first;
            auto comp = components[dropdownId];
            if (comp)
            {
                Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
                dropdown->setWindow(appWindow.get());
                dropdown->setWindowSize(appWindow->getWidth(), appWindow->getHeight());
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value RunEventLoop(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t rootId;
        napi_get_value_int32(env, args[0], &rootId);

        auto root = components[rootId];
        if (root && appWindow)
        {
            appWindow->setRootComponent(root);

            while (!appWindow->shouldClose())
            {
                uv_run(uv_default_loop(), UV_RUN_NOWAIT);

                appWindow->pollEvents();

                if (updateCallback != nullptr && savedEnv != nullptr)
                {
                    napi_handle_scope scope;
                    napi_open_handle_scope(savedEnv, &scope);

                    napi_value callback;
                    napi_get_reference_value(savedEnv, updateCallback, &callback);

                    napi_value global;
                    napi_get_global(savedEnv, &global);

                    napi_call_function(savedEnv, global, callback, 0, nullptr, nullptr);

                    napi_close_handle_scope(savedEnv, scope);
                }

                root->updateScrollbars(0.016f);
                appWindow->render();
                appWindow->swapBuffers();
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetUpdateCallback(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        savedEnv = env;
        napi_create_reference(env, args[0], 1, &updateCallback);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value ParseCSS(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        size_t str_len;
        napi_get_value_string_utf8(env, args[0], nullptr, 0, &str_len);
        char *css_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[0], css_buf, str_len + 1, &str_len);
        std::string css(css_buf);
        delete[] css_buf;

        if (cssParser)
        {
            cssParser->parseString(css);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value ParseCSSFile(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        size_t str_len;
        napi_get_value_string_utf8(env, args[0], nullptr, 0, &str_len);
        char *path_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[0], path_buf, str_len + 1, &str_len);
        std::string path(path_buf);
        delete[] path_buf;

        if (cssParser)
        {
            cssParser->parseFile(path);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value ApplyCSS(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *selector_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], selector_buf, str_len + 1, &str_len);
        std::string selector(selector_buf);
        delete[] selector_buf;

        auto comp = components[id];
        if (comp && cssParser)
        {
            cssParser->applyToStyle(comp->style, selector);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetGradient(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *gradient_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], gradient_buf, str_len + 1, &str_len);
        std::string gradientStr(gradient_buf);
        delete[] gradient_buf;

        auto comp = components[id];
        if (comp && cssParser)
        {
            Gradient gradient = cssParser->parseGradient(gradientStr);
            comp->style.backgroundGradient = gradient;
            comp->style.useGradient = true;
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetFilter(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *filter_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], filter_buf, str_len + 1, &str_len);
        std::string filterStr(filter_buf);
        delete[] filter_buf;

        auto comp = components[id];
        if (comp && cssParser)
        {
            comp->style.filters = cssParser->parseFilters(filterStr);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetBackdropFilter(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *filter_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], filter_buf, str_len + 1, &str_len);
        std::string filterStr(filter_buf);
        delete[] filter_buf;

        auto comp = components[id];
        if (comp && cssParser)
        {
            if (filterStr.find("liquid-blur") != std::string::npos)
            {
                size_t start = filterStr.find("liquid-blur(");
                if (start != std::string::npos)
                {
                    start += 12;
                    size_t end = filterStr.find(')', start);
                    std::string params = filterStr.substr(start, end - start);

                    Filter liquidFilter;
                    liquidFilter.type = FilterType::LiquidBlur;
                    liquidFilter.value = 40.0f;
                    liquidFilter.value2 = 0.3f;
                    liquidFilter.value3 = 0.002f;
                    liquidFilter.value4 = 0.25f;

                    size_t pos = 0;
                    int paramIndex = 0;
                    while (pos < params.length() && paramIndex < 4)
                    {
                        while (pos < params.length() && (params[pos] == ' ' || params[pos] == ','))
                            pos++;

                        size_t numStart = pos;
                        while (pos < params.length() && params[pos] != ' ' && params[pos] != ',')
                            pos++;

                        if (numStart < pos)
                        {
                            std::string numStr = params.substr(numStart, pos - numStart);
                            float value = std::stof(numStr.substr(0, numStr.find("px")));

                            if (paramIndex == 0)
                                liquidFilter.value = value;
                            else if (paramIndex == 1)
                                liquidFilter.value2 = value;
                            else if (paramIndex == 2)
                                liquidFilter.value3 = value;
                            else if (paramIndex == 3)
                                liquidFilter.value4 = value;

                            paramIndex++;
                        }
                    }

                    comp->style.backdropFilters.clear();
                    comp->style.backdropFilters.push_back(liquidFilter);
                }
            }
            else
            {
                comp->style.backdropFilters = cssParser->parseFilters(filterStr);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value CreateButton(napi_env env, napi_callback_info info)
    {
        auto button = std::make_shared<Button>();
        int id = nextComponentId++;
        components[id] = button;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value SetButtonOnClick(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto comp = components[id];
        if (comp)
        {
            Button *button = static_cast<Button *>(comp.get());
            if (button)
            {
                napi_ref callbackRef;
                napi_create_reference(env, args[1], 1, &callbackRef);
                buttonCallbacks[id] = callbackRef;

                button->onClick = [env, callbackRef]()
                {
                    napi_handle_scope scope;
                    napi_open_handle_scope(env, &scope);

                    napi_value callback;
                    napi_get_reference_value(env, callbackRef, &callback);

                    napi_value global;
                    napi_get_global(env, &global);

                    napi_call_function(env, global, callback, 0, nullptr, nullptr);

                    napi_close_handle_scope(env, scope);
                };
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value CreateView(napi_env env, napi_callback_info info)
    {
        auto view = std::make_shared<View>();
        int id = nextComponentId++;
        components[id] = view;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value CreateImage(napi_env env, napi_callback_info info)
    {
        auto image = std::make_shared<Image>();
        int id = nextComponentId++;
        components[id] = image;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value CreateTextBox(napi_env env, napi_callback_info info)
    {
        auto textbox = std::make_shared<TextBox>();
        int id = nextComponentId++;
        components[id] = textbox;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value CreateText(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        std::string text = "";
        if (argc > 0)
        {
            size_t str_len;
            napi_get_value_string_utf8(env, args[0], nullptr, 0, &str_len);
            char *text_buf = new char[str_len + 1];
            napi_get_value_string_utf8(env, args[0], text_buf, str_len + 1, &str_len);
            text = std::string(text_buf);
            delete[] text_buf;
        }

        auto textComp = std::make_shared<Text>(text);
        int id = nextComponentId++;
        components[id] = textComp;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value CreateMenu(napi_env env, napi_callback_info info)
    {
        auto menu = std::make_shared<Menu>();
        int id = nextComponentId++;
        components[id] = menu;
        menuCallbacks[id] = std::vector<napi_ref>();

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value AddMenuItem(napi_env env, napi_callback_info info)
    {
        size_t argc = 4;
        napi_value args[4];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *text_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], text_buf, str_len + 1, &str_len);
        std::string text(text_buf);
        delete[] text_buf;

        std::string icon = "";
        if (argc > 3)
        {
            napi_valuetype type;
            napi_typeof(env, args[3], &type);
            if (type == napi_string)
            {
                napi_get_value_string_utf8(env, args[3], nullptr, 0, &str_len);
                char *icon_buf = new char[str_len + 1];
                napi_get_value_string_utf8(env, args[3], icon_buf, str_len + 1, &str_len);
                icon = std::string(icon_buf);
                delete[] icon_buf;
            }
        }

        auto comp = components[id];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                napi_ref callbackRef;
                napi_create_reference(env, args[2], 1, &callbackRef);
                menuCallbacks[id].push_back(callbackRef);

                menu->addItem(text, [env, callbackRef]()
                              {
                napi_handle_scope scope;
                napi_open_handle_scope(env, &scope);
                
                napi_value callback;
                napi_get_reference_value(env, callbackRef, &callback);
                
                napi_value global;
                napi_get_global(env, &global);
                
                napi_call_function(env, global, callback, 0, nullptr, nullptr);
                
                napi_close_handle_scope(env, scope); }, nullptr, icon);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuItemIcon(napi_env env, napi_callback_info info)
    {
        size_t argc = 3;
        napi_value args[3];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t menuId, itemIndex;
        napi_get_value_int32(env, args[0], &menuId);
        napi_get_value_int32(env, args[1], &itemIndex);

        size_t str_len;
        napi_get_value_string_utf8(env, args[2], nullptr, 0, &str_len);
        char *icon_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[2], icon_buf, str_len + 1, &str_len);
        std::string iconPath(icon_buf);
        delete[] icon_buf;

        auto comp = components[menuId];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->setItemIcon(itemIndex, iconPath);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuItemChecked(napi_env env, napi_callback_info info)
    {
        size_t argc = 4;
        napi_value args[4];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t menuId, itemIndex;
        napi_get_value_int32(env, args[0], &menuId);
        napi_get_value_int32(env, args[1], &itemIndex);

        bool checked;
        napi_get_value_bool(env, args[2], &checked);

        std::string checkIcon = "";
        if (argc > 3)
        {
            size_t str_len;
            napi_get_value_string_utf8(env, args[3], nullptr, 0, &str_len);
            char *icon_buf = new char[str_len + 1];
            napi_get_value_string_utf8(env, args[3], icon_buf, str_len + 1, &str_len);
            checkIcon = std::string(icon_buf);
            delete[] icon_buf;
        }

        auto comp = components[menuId];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->setItemChecked(itemIndex, checked, checkIcon);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuIconsVisible(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t menuId;
        napi_get_value_int32(env, args[0], &menuId);

        bool visible;
        napi_get_value_bool(env, args[1], &visible);

        auto comp = components[menuId];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->setIconsVisible(visible);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuCheckIconsVisible(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t menuId;
        napi_get_value_int32(env, args[0], &menuId);

        bool visible;
        napi_get_value_bool(env, args[1], &visible);

        auto comp = components[menuId];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->setCheckIconsVisible(visible);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuIconSize(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double size;
        napi_get_value_double(env, args[0], &size);

        Menu::setGlobalIconSize((float)size);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuCheckIconSize(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double size;
        napi_get_value_double(env, args[0], &size);

        Menu::setGlobalCheckIconSize((float)size);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuIconMargin(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double margin;
        napi_get_value_double(env, args[0], &margin);

        Menu::setGlobalIconMargin((float)margin);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuCheckIconMargin(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double margin;
        napi_get_value_double(env, args[0], &margin);

        Menu::setGlobalCheckIconMargin((float)margin);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value AddMenuSeparator(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto comp = components[id];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->addSeparator();
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value AddMenuSubmenu(napi_env env, napi_callback_info info)
    {
        size_t argc = 3;
        napi_value args[3];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t parentId, submenuId;
        napi_get_value_int32(env, args[0], &parentId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *text_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], text_buf, str_len + 1, &str_len);
        std::string text(text_buf);
        delete[] text_buf;

        napi_get_value_int32(env, args[2], &submenuId);

        auto parentComp = components[parentId];
        auto submenuComp = components[submenuId];

        if (parentComp && submenuComp)
        {
            Menu *parentMenu = static_cast<Menu *>(parentComp.get());
            Menu *submenu = static_cast<Menu *>(submenuComp.get());

            if (parentMenu && submenu)
            {
                parentMenu->addSubmenu(text, std::shared_ptr<Menu>(submenuComp, submenu));
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value ShowMenu(napi_env env, napi_callback_info info)
    {
        size_t argc = 5;
        napi_value args[5];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        double x, y;
        napi_get_value_double(env, args[1], &x);
        napi_get_value_double(env, args[2], &y);

        int32_t windowWidth, windowHeight;
        napi_get_value_int32(env, args[3], &windowWidth);
        napi_get_value_int32(env, args[4], &windowHeight);

        auto comp = components[id];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->show((float)x, (float)y, windowWidth, windowHeight);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value HideMenu(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto comp = components[id];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->hide();
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuFont(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *font_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], font_buf, str_len + 1, &str_len);
        std::string fontName(font_buf);
        delete[] font_buf;

        auto comp = components[id];
        if (comp)
        {
            Menu *menu = static_cast<Menu *>(comp.get());
            if (menu)
            {
                menu->setFont(fontName);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    void ApplyStyleObject(napi_env env, napi_value styleObj, Style &style)
    {
        napi_value value;

        if (napi_get_named_property(env, styleObj, "backgroundColor", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
                style.backgroundColor = Color::FromHex(colorStr);
        }
        if (napi_get_named_property(env, styleObj, "color", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
                style.color = Color::FromHex(colorStr);
        }
        if (napi_get_named_property(env, styleObj, "width", &value) == napi_ok)
        {
            napi_valuetype type;
            napi_typeof(env, value, &type);
            if (type == napi_string)
            {
                char widthStr[32];
                size_t len;
                if (napi_get_value_string_utf8(env, value, widthStr, 32, &len) == napi_ok)
                {
                    std::string w(widthStr);
                    if (w.find('%') != std::string::npos)
                    {
                        style.width = std::stof(w.substr(0, w.find('%')));
                        style.widthIsPercent = true;
                    }
                    else
                    {
                        style.width = std::stof(w);
                        style.widthIsPercent = false;
                    }
                }
            }
            else if (type == napi_number)
            {
                double width;
                if (napi_get_value_double(env, value, &width) == napi_ok)
                {
                    style.width = (float)width;
                    style.widthIsPercent = false;
                }
            }
        }
        if (napi_get_named_property(env, styleObj, "height", &value) == napi_ok)
        {
            napi_valuetype type;
            napi_typeof(env, value, &type);
            if (type == napi_string)
            {
                char heightStr[32];
                size_t len;
                if (napi_get_value_string_utf8(env, value, heightStr, 32, &len) == napi_ok)
                {
                    std::string h(heightStr);
                    if (h.find('%') != std::string::npos)
                    {
                        style.height = std::stof(h.substr(0, h.find('%')));
                        style.heightIsPercent = true;
                    }
                    else
                    {
                        style.height = std::stof(h);
                        style.heightIsPercent = false;
                    }
                }
            }
            else if (type == napi_number)
            {
                double height;
                if (napi_get_value_double(env, value, &height) == napi_ok)
                {
                    style.height = (float)height;
                    style.heightIsPercent = false;
                }
            }
        }
        if (napi_get_named_property(env, styleObj, "minWidth", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.minWidth = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "minHeight", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.minHeight = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "maxWidth", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.maxWidth = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "maxHeight", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.maxHeight = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "borderWidth", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.borderWidth = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "borderColor", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
                style.borderColor = Color::FromHex(colorStr);
        }
        if (napi_get_named_property(env, styleObj, "borderRadius", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.borderRadius = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "padding", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                style.padding.top = style.padding.right = style.padding.bottom = style.padding.left = (float)v;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingTop", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.padding.top = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "paddingRight", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.padding.right = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "paddingBottom", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.padding.bottom = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "paddingLeft", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.padding.left = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "margin", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                style.margin.top = style.margin.right = style.margin.bottom = style.margin.left = (float)v;
            }
        }
        if (napi_get_named_property(env, styleObj, "marginTop", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.margin.top = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "marginRight", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.margin.right = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "marginBottom", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.margin.bottom = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "marginLeft", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.margin.left = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "fontSize", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.fontSize = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "fontWeight", &value) == napi_ok)
        {
            char weightStr[16];
            size_t len;
            if (napi_get_value_string_utf8(env, value, weightStr, 16, &len) == napi_ok)
                style.fontWeight = std::string(weightStr);
        }
        if (napi_get_named_property(env, styleObj, "opacity", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.opacity = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "gap", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.gap = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "rowGap", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.rowGap = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "columnGap", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.columnGap = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "top", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.top = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "bottom", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
                style.bottom = (float)v;
        }
        if (napi_get_named_property(env, styleObj, "left", &value) == napi_ok)
        {
            napi_valuetype type;
            napi_typeof(env, value, &type);
            if (type == napi_string)
            {
                char leftStr[32];
                size_t len;
                if (napi_get_value_string_utf8(env, value, leftStr, 32, &len) == napi_ok)
                {
                    std::string l(leftStr);
                    if (l.find('%') != std::string::npos)
                    {
                        style.left = std::stof(l.substr(0, l.find('%')));
                        style.leftIsPercent = true;
                    }
                    else
                    {
                        style.left = std::stof(l);
                        style.leftIsPercent = false;
                    }
                }
            }
            else if (type == napi_number)
            {
                double v;
                if (napi_get_value_double(env, value, &v) == napi_ok)
                {
                    style.left = (float)v;
                    style.leftIsPercent = false;
                }
            }
        }
        if (napi_get_named_property(env, styleObj, "right", &value) == napi_ok)
        {
            napi_valuetype type;
            napi_typeof(env, value, &type);
            if (type == napi_string)
            {
                char rightStr[32];
                size_t len;
                if (napi_get_value_string_utf8(env, value, rightStr, 32, &len) == napi_ok)
                {
                    std::string r(rightStr);
                    if (r.find('%') != std::string::npos)
                    {
                        style.right = std::stof(r.substr(0, r.find('%')));
                        style.rightIsPercent = true;
                    }
                    else
                    {
                        style.right = std::stof(r);
                        style.rightIsPercent = false;
                    }
                }
            }
            else if (type == napi_number)
            {
                double v;
                if (napi_get_value_double(env, value, &v) == napi_ok)
                {
                    style.right = (float)v;
                    style.rightIsPercent = false;
                }
            }
        }
        if (napi_get_named_property(env, styleObj, "flexDirection", &value) == napi_ok)
        {
            char str[16];
            size_t len;
            if (napi_get_value_string_utf8(env, value, str, 16, &len) == napi_ok)
            {
                std::string s(str);
                if (s == "row")
                    style.flexDirection = FlexDirection::Row;
                else if (s == "column")
                    style.flexDirection = FlexDirection::Column;
            }
        }
        if (napi_get_named_property(env, styleObj, "justifyContent", &value) == napi_ok)
        {
            char str[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, str, 32, &len) == napi_ok)
            {
                std::string s(str);
                if (s == "center")
                    style.justifyContent = JustifyContent::Center;
                else if (s == "flex-start")
                    style.justifyContent = JustifyContent::FlexStart;
                else if (s == "flex-end")
                    style.justifyContent = JustifyContent::FlexEnd;
                else if (s == "space-between")
                    style.justifyContent = JustifyContent::SpaceBetween;
                else if (s == "space-around")
                    style.justifyContent = JustifyContent::SpaceAround;
                else if (s == "space-evenly")
                    style.justifyContent = JustifyContent::SpaceEvenly;
            }
        }
        if (napi_get_named_property(env, styleObj, "alignItems", &value) == napi_ok)
        {
            char str[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, str, 32, &len) == napi_ok)
            {
                std::string s(str);
                if (s == "center")
                    style.alignItems = AlignItems::Center;
                else if (s == "flex-start")
                    style.alignItems = AlignItems::FlexStart;
                else if (s == "flex-end")
                    style.alignItems = AlignItems::FlexEnd;
                else if (s == "stretch")
                    style.alignItems = AlignItems::Stretch;
                else if (s == "baseline")
                    style.alignItems = AlignItems::Baseline;
            }
        }
        if (napi_get_named_property(env, styleObj, "position", &value) == napi_ok)
        {
            char str[16];
            size_t len;
            if (napi_get_value_string_utf8(env, value, str, 16, &len) == napi_ok)
            {
                std::string s(str);
                if (s == "absolute")
                    style.position = Position::Absolute;
                else if (s == "fixed")
                    style.position = Position::Fixed;
                else if (s == "relative")
                    style.position = Position::Relative;
                else
                    style.position = Position::Static;
            }
        }
        if (napi_get_named_property(env, styleObj, "overflow", &value) == napi_ok)
        {
            char str[16];
            size_t len;
            if (napi_get_value_string_utf8(env, value, str, 16, &len) == napi_ok)
            {
                std::string s(str);
                if (s == "hidden")
                {
                    style.overflow = Overflow::Hidden;
                    style.overflowX = Overflow::Hidden;
                    style.overflowY = Overflow::Hidden;
                }
                else if (s == "scroll")
                {
                    style.overflow = Overflow::Scroll;
                    style.overflowX = Overflow::Scroll;
                    style.overflowY = Overflow::Scroll;
                }
                else if (s == "auto")
                {
                    style.overflow = Overflow::Auto;
                    style.overflowX = Overflow::Auto;
                    style.overflowY = Overflow::Auto;
                }
            }
        }
        if (napi_get_named_property(env, styleObj, "visibility", &value) == napi_ok)
        {
            napi_valuetype visType;
            napi_typeof(env, value, &visType);
            if (visType == napi_string)
            {
                char str[16];
                size_t len;
                if (napi_get_value_string_utf8(env, value, str, 16, &len) == napi_ok)
                {
                    std::string s(str);
                    if (s == "hidden")
                        style.visibility = Style::Visibility::Hidden;
                    else if (s == "collapsed")
                        style.visibility = Style::Visibility::Collapsed;
                    else
                        style.visibility = Style::Visibility::Visible;
                }
            }
        }
        if (napi_get_named_property(env, styleObj, "transformOrigin", &value) == napi_ok)
        {
            char str[64];
            size_t len;
            if (napi_get_value_string_utf8(env, value, str, 64, &len) == napi_ok)
                style.transformOrigin = std::string(str);
        }
        if (napi_get_named_property(env, styleObj, "transform", &value) == napi_ok)
        {
            char transformStr[256];
            size_t len;
            if (napi_get_value_string_utf8(env, value, transformStr, 256, &len) == napi_ok)
            {
                std::string transform(transformStr);

                size_t pos = transform.find("translateX(");
                if (pos != std::string::npos)
                {
                    size_t start = pos + 11, end = transform.find(')', start);
                    std::string val = transform.substr(start, end - start);
                    if (val.find('%') != std::string::npos)
                    {
                        style.translateX = std::stof(val.substr(0, val.find('%')));
                        style.translateXIsPercent = true;
                    }
                    else if (val.find("px") != std::string::npos)
                    {
                        style.translateX = std::stof(val.substr(0, val.find("px")));
                        style.translateXIsPercent = false;
                    }
                    else
                    {
                        style.translateX = std::stof(val);
                        style.translateXIsPercent = false;
                    }
                }
                pos = transform.find("translateY(");
                if (pos != std::string::npos)
                {
                    size_t start = pos + 11, end = transform.find(')', start);
                    std::string val = transform.substr(start, end - start);
                    if (val.find('%') != std::string::npos)
                    {
                        style.translateY = std::stof(val.substr(0, val.find('%')));
                        style.translateYIsPercent = true;
                    }
                    else if (val.find("px") != std::string::npos)
                    {
                        style.translateY = std::stof(val.substr(0, val.find("px")));
                        style.translateYIsPercent = false;
                    }
                    else
                    {
                        style.translateY = std::stof(val);
                        style.translateYIsPercent = false;
                    }
                }
                pos = transform.find("scaleX(");
                if (pos != std::string::npos)
                {
                    size_t start = pos + 7, end = transform.find(')', start);
                    style.scaleX = std::stof(transform.substr(start, end - start));
                }

                pos = transform.find("scaleY(");
                if (pos != std::string::npos)
                {
                    size_t start = pos + 7, end = transform.find(')', start);
                    style.scaleY = std::stof(transform.substr(start, end - start));
                }

                pos = transform.find("rotate(");
                if (pos != std::string::npos)
                {
                    size_t start = pos + 7, end = transform.find(')', start);
                    std::string val = transform.substr(start, end - start);
                    style.rotate = std::stof(val.substr(0, val.find("deg")));
                }
            }
        }
        if (napi_get_named_property(env, styleObj, "backdropFilter", &value) == napi_ok)
        {
            char filterStr[256];
            size_t len;
            if (napi_get_value_string_utf8(env, value, filterStr, 256, &len) == napi_ok)
            {
                std::string filter(filterStr);
                size_t blurPos = filter.find("blur(");
                if (blurPos != std::string::npos)
                {
                    size_t start = blurPos + 5, end = filter.find(')', start);
                    std::string val = filter.substr(start, end - start);
                    float blurAmount = std::stof(val.substr(0, val.find("px")));
                    Filter blurFilter;
                    blurFilter.type = FilterType::Blur;
                    blurFilter.value = blurAmount;
                    style.backdropFilters.clear();
                    style.backdropFilters.push_back(blurFilter);
                }
            }
        }
    }

    napi_value SetComponentStyle(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto comp = components[id];
        if (!comp)
        {
            napi_value result;
            napi_get_undefined(env, &result);
            return result;
        }

        ApplyStyleObject(env, args[1], comp->style);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetButtonText(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *text_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], text_buf, str_len + 1, &str_len);
        std::string text(text_buf);
        delete[] text_buf;

        auto comp = components[id];
        if (comp)
        {
            Button *button = static_cast<Button *>(comp.get());
            if (button)
            {
                auto textComp = std::make_shared<Text>(text);
                textComp->style.fontSize = comp->style.fontSize > 0 ? comp->style.fontSize : 16;
                textComp->style.color = comp->style.color;
                comp->children.clear();
                comp->addChild(textComp);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetImageSource(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *path_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], path_buf, str_len + 1, &str_len);
        std::string path(path_buf);
        delete[] path_buf;

        auto comp = components[id];
        if (comp)
        {
            Image *img = static_cast<Image *>(comp.get());
            if (img)
            {
                img->setSource(path);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetText(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *text_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], text_buf, str_len + 1, &str_len);
        std::string text(text_buf);
        delete[] text_buf;

        auto comp = components[id];
        if (comp)
        {
            Text *textComp = static_cast<Text *>(comp.get());
            if (textComp)
            {
                textComp->setText(text);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetHoverStyle(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto comp = components[id];
        if (!comp)
        {
            napi_value result;
            napi_get_undefined(env, &result);
            return result;
        }

        napi_value result;
        napi_get_undefined(env, &result);

        Button *button = static_cast<Button *>(comp.get());
        if (!button)
        {
            napi_get_undefined(env, &result);
            return result;
        }

        napi_value styleObj = args[1];
        napi_value value;

        if (napi_get_named_property(env, styleObj, "backgroundColor", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
            {
                button->hoverStyle.backgroundColor = Color::FromHex(colorStr);
                button->hoverOverrides.backgroundColor = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "color", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
            {
                button->hoverStyle.color = Color::FromHex(colorStr);
                button->hoverOverrides.color = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "borderRadius", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.borderRadius = (float)v;
                button->hoverOverrides.borderRadius = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "opacity", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.opacity = (float)v;
                button->hoverOverrides.opacity = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "borderWidth", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.borderWidth = (float)v;
                button->hoverOverrides.borderWidth = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "borderColor", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
            {
                button->hoverStyle.borderColor = Color::FromHex(colorStr);
                button->hoverOverrides.borderColor = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "fontSize", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.fontSize = (float)v;
                button->hoverOverrides.fontSize = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingTop", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.padding.top = (float)v;
                button->hoverOverrides.paddingTop = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingRight", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.padding.right = (float)v;
                button->hoverOverrides.paddingRight = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingBottom", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.padding.bottom = (float)v;
                button->hoverOverrides.paddingBottom = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingLeft", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->hoverStyle.padding.left = (float)v;
                button->hoverOverrides.paddingLeft = true;
            }
        }

        return result;
    }

    napi_value SetPressedStyle(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto comp = components[id];
        if (!comp)
        {
            napi_value result;
            napi_get_undefined(env, &result);
            return result;
        }

        napi_value result;
        napi_get_undefined(env, &result);

        Button *button = static_cast<Button *>(comp.get());
        if (!button)
            return result;

        napi_value styleObj = args[1];
        napi_value value;

        if (napi_get_named_property(env, styleObj, "backgroundColor", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
            {
                button->pressedStyle.backgroundColor = Color::FromHex(colorStr);
                button->pressedOverrides.backgroundColor = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "color", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
            {
                button->pressedStyle.color = Color::FromHex(colorStr);
                button->pressedOverrides.color = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "borderRadius", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.borderRadius = (float)v;
                button->pressedOverrides.borderRadius = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "opacity", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.opacity = (float)v;
                button->pressedOverrides.opacity = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "borderWidth", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.borderWidth = (float)v;
                button->pressedOverrides.borderWidth = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "borderColor", &value) == napi_ok)
        {
            char colorStr[32];
            size_t len;
            if (napi_get_value_string_utf8(env, value, colorStr, 32, &len) == napi_ok)
            {
                button->pressedStyle.borderColor = Color::FromHex(colorStr);
                button->pressedOverrides.borderColor = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "fontSize", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.fontSize = (float)v;
                button->pressedOverrides.fontSize = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingTop", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.padding.top = (float)v;
                button->pressedOverrides.paddingTop = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingRight", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.padding.right = (float)v;
                button->pressedOverrides.paddingRight = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingBottom", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.padding.bottom = (float)v;
                button->pressedOverrides.paddingBottom = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "paddingLeft", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.padding.left = (float)v;
                button->pressedOverrides.paddingLeft = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "marginTop", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.margin.top = (float)v;
                button->pressedOverrides.marginTop = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "marginRight", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.margin.right = (float)v;
                button->pressedOverrides.marginRight = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "marginBottom", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.margin.bottom = (float)v;
                button->pressedOverrides.marginBottom = true;
            }
        }
        if (napi_get_named_property(env, styleObj, "marginLeft", &value) == napi_ok)
        {
            double v;
            if (napi_get_value_double(env, value, &v) == napi_ok)
            {
                button->pressedStyle.margin.left = (float)v;
                button->pressedOverrides.marginLeft = true;
            }
        }

        return result;
    }

    napi_value AddChild(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t parentId, childId;
        napi_get_value_int32(env, args[0], &parentId);
        napi_get_value_int32(env, args[1], &childId);

        auto parent = components[parentId];
        auto child = components[childId];

        if (parent && child)
        {
            parent->addChild(child);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value Render(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t rootId;
        napi_get_value_int32(env, args[0], &rootId);

        auto root = components[rootId];
        if (root && appWindow)
        {
            appWindow->pollEvents();
            root->updateScrollbars(0.016f);
            appWindow->setRootComponent(root);
            appWindow->render();
            appWindow->swapBuffers();
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value ShouldClose(napi_env env, napi_callback_info info)
    {
        napi_value result;
        if (appWindow && appWindow->shouldClose())
        {
            napi_get_boolean(env, true, &result);
        }
        else
        {
            napi_get_boolean(env, false, &result);
        }
        return result;
    }

    napi_value SetVisibility(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        char visStr[16];
        size_t len;
        napi_get_value_string_utf8(env, args[1], visStr, 16, &len);
        std::string vis(visStr);

        auto comp = components[id];
        if (comp)
        {
            if (vis == "hidden")
                comp->style.visibility = Style::Visibility::Hidden;
            else if (vis == "collapsed")
                comp->style.visibility = Style::Visibility::Collapsed;
            else
                comp->style.visibility = Style::Visibility::Visible;
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value CreateDropdown(napi_env env, napi_callback_info info)
    {
        auto dropdown = std::make_shared<Dropdown>();

        if (appWindow)
        {
            dropdown->setWindow(appWindow.get());
            dropdown->setWindowSize(appWindow->getWidth(), appWindow->getHeight());
        }

        int id = nextComponentId++;
        components[id] = dropdown;
        dropdownCallbacks[id] = std::vector<napi_ref>();

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value AddDropdownOption(napi_env env, napi_callback_info info)
    {
        size_t argc = 3;
        napi_value args[3];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *text_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], text_buf, str_len + 1, &str_len);
        std::string text(text_buf);
        delete[] text_buf;

        std::string value = text;
        if (argc > 2)
        {
            napi_valuetype valueType;
            napi_typeof(env, args[2], &valueType);
            if (valueType == napi_string)
            {
                napi_get_value_string_utf8(env, args[2], nullptr, 0, &str_len);
                char *value_buf = new char[str_len + 1];
                napi_get_value_string_utf8(env, args[2], value_buf, str_len + 1, &str_len);
                value = std::string(value_buf);
                delete[] value_buf;
            }
        }

        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                dropdown->addOption(text, value);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetDropdownSelectedIndex(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id, index;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_int32(env, args[1], &index);

        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                dropdown->setSelectedIndex(index);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetDropdownSelectedValue(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *value_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], value_buf, str_len + 1, &str_len);
        std::string value(value_buf);
        delete[] value_buf;

        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                dropdown->setSelectedValue(value);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value GetDropdownSelectedIndex(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        int selectedIndex = -1;
        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                selectedIndex = dropdown->getSelectedIndex();
            }
        }

        napi_value result;
        napi_create_int32(env, selectedIndex, &result);
        return result;
    }

    napi_value GetDropdownSelectedValue(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        std::string selectedValue = "";
        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                selectedValue = dropdown->getSelectedValue();
            }
        }

        napi_value result;
        napi_create_string_utf8(env, selectedValue.c_str(), selectedValue.length(), &result);
        return result;
    }

    napi_value SetDropdownPlaceholder(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *text_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], text_buf, str_len + 1, &str_len);
        std::string text(text_buf);
        delete[] text_buf;

        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                dropdown->setPlaceholder(text);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetDropdownOnChange(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                napi_ref callbackRef;
                napi_create_reference(env, args[1], 1, &callbackRef);
                dropdownCallbacks[id].push_back(callbackRef);

                dropdown->setOnChange([env, callbackRef](int index, const std::string &value)
                                      {
                napi_handle_scope scope;
                napi_open_handle_scope(env, &scope);
                
                napi_value callback;
                napi_get_reference_value(env, callbackRef, &callback);
                
                napi_value global;
                napi_get_global(env, &global);
                
                napi_value argv[2];
                napi_create_int32(env, index, &argv[0]);
                napi_create_string_utf8(env, value.c_str(), value.length(), &argv[1]);
                
                napi_call_function(env, global, callback, 2, argv, nullptr);
                
                napi_close_handle_scope(env, scope); });
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetDropdownFont(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *font_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], font_buf, str_len + 1, &str_len);
        std::string fontName(font_buf);
        delete[] font_buf;

        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                dropdown->setFont(fontName);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetDropdownWindowSize(napi_env env, napi_callback_info info)
    {
        size_t argc = 3;
        napi_value args[3];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id, width, height;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_int32(env, args[1], &width);
        napi_get_value_int32(env, args[2], &height);

        auto comp = components[id];
        if (comp)
        {
            Dropdown *dropdown = static_cast<Dropdown *>(comp.get());
            if (dropdown)
            {
                dropdown->setWindowSize(width, height);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value LoadAudio(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        size_t str_len;
        napi_get_value_string_utf8(env, args[0], nullptr, 0, &str_len);
        char *path_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[0], path_buf, str_len + 1, &str_len);
        std::string path(path_buf);
        delete[] path_buf;

        auto &audioMgr = AudioManager::getInstance();
        auto audioFile = audioMgr.source.load(path);

        int id = nextAudioId++;
        audioFiles[id] = audioFile;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value UnloadAudio(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            auto &audioMgr = AudioManager::getInstance();
            audioMgr.source.unload(it->second->getFilePath());
            audioFiles.erase(it);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value PlayAudio(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            it->second->play();
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value PauseAudio(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            it->second->pause();
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value StopAudio(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            it->second->stop();
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value IsAudioPlaying(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        bool playing = false;
        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            playing = it->second->isPlaying();
        }

        napi_value result;
        napi_get_boolean(env, playing, &result);
        return result;
    }

    napi_value GetAudioCurrentTime(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        double time = 0.0;
        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            time = it->second->getCurrentTime();
        }

        napi_value result;
        napi_create_double(env, time, &result);
        return result;
    }

    napi_value SetAudioCurrentTime(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        double time;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_double(env, args[1], &time);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            it->second->setCurrentTime((float)time);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value GetAudioDuration(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        double duration = 0.0;
        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            duration = it->second->getDuration();
        }

        napi_value result;
        napi_create_double(env, duration, &result);
        return result;
    }

    napi_value SetAudioLoop(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        bool loop;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_bool(env, args[1], &loop);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            it->second->setLoop(loop);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetAudioSpeed(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        double speed;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_double(env, args[1], &speed);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            it->second->setSpeed((float)speed);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetAudioVolume(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        double volume;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_double(env, args[1], &volume);

        auto it = audioFiles.find(id);
        if (it != audioFiles.end())
        {
            it->second->setVolume((float)volume);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value GetSystemVolume(napi_env env, napi_callback_info info)
    {
        float volume = AudioManager::GetSystemVolume();

        napi_value result;
        napi_create_double(env, volume, &result);
        return result;
    }

    napi_value SetSystemVolume(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        double volume;
        napi_get_value_double(env, args[0], &volume);

        AudioManager::SetSystemVolume((float)volume);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value GetOutputDevices(napi_env env, napi_callback_info info)
    {
        auto &audioMgr = AudioManager::getInstance();
        auto devices = audioMgr.getOutputDevices();

        napi_value result;
        napi_create_array_with_length(env, devices.size(), &result);

        for (size_t i = 0; i < devices.size(); i++)
        {
            napi_value device;
            napi_create_string_utf8(env, devices[i].c_str(), devices[i].length(), &device);
            napi_set_element(env, result, i, device);
        }

        return result;
    }

    napi_value GetInputDevices(napi_env env, napi_callback_info info)
    {
        auto &audioMgr = AudioManager::getInstance();
        auto devices = audioMgr.getInputDevices();

        napi_value result;
        napi_create_array_with_length(env, devices.size(), &result);

        for (size_t i = 0; i < devices.size(); i++)
        {
            napi_value device;
            napi_create_string_utf8(env, devices[i].c_str(), devices[i].length(), &device);
            napi_set_element(env, result, i, device);
        }

        return result;
    }

    napi_value SetOutputDevice(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        size_t str_len;
        napi_get_value_string_utf8(env, args[0], nullptr, 0, &str_len);
        char *device_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[0], device_buf, str_len + 1, &str_len);
        std::string deviceName(device_buf);
        delete[] device_buf;

        auto &audioMgr = AudioManager::getInstance();
        audioMgr.setOutputDevice(deviceName);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value CreateManagedWindow(napi_env env, napi_callback_info info)
    {
        size_t argc = 9;
        napi_value args[9];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t contentId;
        napi_get_value_int32(env, args[0], &contentId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *title_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], title_buf, str_len + 1, &str_len);
        std::string title(title_buf);
        delete[] title_buf;

        double x, y, w, h;
        napi_get_value_double(env, args[2], &x);
        napi_get_value_double(env, args[3], &y);
        napi_get_value_double(env, args[4], &w);
        napi_get_value_double(env, args[5], &h);

        bool hasFrame = true;
        if (argc > 6)
        {
            napi_valuetype t;
            napi_typeof(env, args[6], &t);
            if (t == napi_boolean)
                napi_get_value_bool(env, args[6], &hasFrame);
        }

        bool alwaysOnTop = false;
        if (argc > 7)
        {
            napi_valuetype t;
            napi_typeof(env, args[7], &t);
            if (t == napi_boolean)
                napi_get_value_bool(env, args[7], &alwaysOnTop);
        }

        bool transparent = false;
        if (argc > 8)
        {
            napi_valuetype t;
            napi_typeof(env, args[8], &t);
            if (t == napi_boolean)
                napi_get_value_bool(env, args[8], &transparent);
        }

        auto content = components[contentId];
        if (content && appWindow)
        {
            WindowManager *wm = appWindow->getWindowManager();
            if (wm)
            {
                wm->addWindow(content, title, (float)x, (float)y, (float)w, (float)h, hasFrame, alwaysOnTop, transparent);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetWindowZOrder(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t windowIndex, zOrder;
        napi_get_value_int32(env, args[0], &windowIndex);
        napi_get_value_int32(env, args[1], &zOrder);

        if (appWindow)
        {
            WindowManager *wm = appWindow->getWindowManager();
            if (wm)
            {
                wm->setWindowZOrder(windowIndex, zOrder);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value GetWindowCount(napi_env env, napi_callback_info info)
    {
        int count = 0;
        if (appWindow)
        {
            WindowManager *wm = appWindow->getWindowManager();
            if (wm)
            {
                count = wm->getWindowCount();
            }
        }

        napi_value result;
        napi_create_int32(env, count, &result);
        return result;
    }

    napi_value CreateAppWindow(napi_env env, napi_callback_info info)
    {
        size_t argc = 9;
        napi_value args[9];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t contentId;
        napi_get_value_int32(env, args[0], &contentId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *title_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], title_buf, str_len + 1, &str_len);
        std::string title(title_buf);
        delete[] title_buf;

        double x, y, w, h;
        napi_get_value_double(env, args[2], &x);
        napi_get_value_double(env, args[3], &y);
        napi_get_value_double(env, args[4], &w);
        napi_get_value_double(env, args[5], &h);

        bool hasFrame = true;
        if (argc > 6)
        {
            napi_valuetype t;
            napi_typeof(env, args[6], &t);
            if (t == napi_boolean)
                napi_get_value_bool(env, args[6], &hasFrame);
        }

        bool alwaysOnTop = false;
        if (argc > 7)
        {
            napi_valuetype t;
            napi_typeof(env, args[7], &t);
            if (t == napi_boolean)
                napi_get_value_bool(env, args[7], &alwaysOnTop);
        }

        bool transparent = false;
        if (argc > 8)
        {
            napi_valuetype t;
            napi_typeof(env, args[8], &t);
            if (t == napi_boolean)
                napi_get_value_bool(env, args[8], &transparent);
        }

        auto content = components[contentId];
        if (content && appWindow)
        {
            WindowManager *wm = appWindow->getWindowManager();
            if (wm)
            {
                wm->addWindow(content, title, (float)x, (float)y, (float)w, (float)h, hasFrame, alwaysOnTop, transparent);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value CreateMenuBar(napi_env env, napi_callback_info info)
    {
        auto menubar = std::make_shared<MenuBar>();

        if (appWindow)
        {
            menubar->setWindowSize(appWindow->getWidth(), appWindow->getHeight());
        }

        int id = nextComponentId++;
        components[id] = menubar;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value AddMenuBarMenu(napi_env env, napi_callback_info info)
    {
        size_t argc = 4;
        napi_value args[4];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t menubarId, menuId;
        napi_get_value_int32(env, args[0], &menubarId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *text_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], text_buf, str_len + 1, &str_len);
        std::string text(text_buf);
        delete[] text_buf;

        napi_get_value_int32(env, args[2], &menuId);

        std::string imagePath = "";
        if (argc > 3)
        {
            napi_valuetype type;
            napi_typeof(env, args[3], &type);
            if (type == napi_string)
            {
                napi_get_value_string_utf8(env, args[3], nullptr, 0, &str_len);
                char *path_buf = new char[str_len + 1];
                napi_get_value_string_utf8(env, args[3], path_buf, str_len + 1, &str_len);
                imagePath = std::string(path_buf);
                delete[] path_buf;
            }
        }

        auto menubarComp = components[menubarId];
        auto menuComp = components[menuId];

        if (menubarComp && menuComp)
        {
            MenuBar *menubar = static_cast<MenuBar *>(menubarComp.get());
            Menu *menu = static_cast<Menu *>(menuComp.get());

            if (menubar && menu)
            {
                menubar->addMenu(text, std::shared_ptr<Menu>(menuComp, menu), imagePath);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuBarFont(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t menubarId;
        napi_get_value_int32(env, args[0], &menubarId);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *font_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], font_buf, str_len + 1, &str_len);
        std::string fontName(font_buf);
        delete[] font_buf;

        auto comp = components[menubarId];
        if (comp)
        {
            MenuBar *menubar = static_cast<MenuBar *>(comp.get());
            if (menubar)
            {
                menubar->setFont(fontName);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetMenuBarWindowSize(napi_env env, napi_callback_info info)
    {
        size_t argc = 3;
        napi_value args[3];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t menubarId, width, height;
        napi_get_value_int32(env, args[0], &menubarId);
        napi_get_value_int32(env, args[1], &width);
        napi_get_value_int32(env, args[2], &height);

        auto comp = components[menubarId];
        if (comp)
        {
            MenuBar *menubar = static_cast<MenuBar *>(comp.get());
            if (menubar)
            {
                menubar->setWindowSize(width, height);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value CreateProgressBar(napi_env env, napi_callback_info info)
    {
        auto progressbar = std::make_shared<ProgressBar>();
        int id = nextComponentId++;
        components[id] = progressbar;

        napi_value result;
        napi_create_int32(env, id, &result);
        return result;
    }

    napi_value SetProgressBarValue(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        double value;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_double(env, args[1], &value);

        auto comp = components[id];
        if (comp)
        {
            ProgressBar *pb = static_cast<ProgressBar *>(comp.get());
            if (pb)
            {
                pb->setValue((float)value);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value GetProgressBarValue(napi_env env, napi_callback_info info)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        double value = 0.0;
        auto comp = components[id];
        if (comp)
        {
            ProgressBar *pb = static_cast<ProgressBar *>(comp.get());
            if (pb)
            {
                value = pb->getValue();
            }
        }

        napi_value result;
        napi_create_double(env, value, &result);
        return result;
    }

    napi_value UpdateProgressBar(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        double deltaTime;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_double(env, args[1], &deltaTime);

        auto comp = components[id];
        if (comp)
        {
            ProgressBar *pb = static_cast<ProgressBar *>(comp.get());
            if (pb)
            {
                pb->update((float)deltaTime);
            }
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value AnimateComponent(napi_env env, napi_callback_info info)
    {
        size_t argc = 6;
        napi_value args[6];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id;
        napi_get_value_int32(env, args[0], &id);

        size_t str_len;
        napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_len);
        char *prop_buf = new char[str_len + 1];
        napi_get_value_string_utf8(env, args[1], prop_buf, str_len + 1, &str_len);
        std::string propertyStr(prop_buf);
        delete[] prop_buf;

        double startVal, endVal, duration;
        napi_get_value_double(env, args[2], &startVal);
        napi_get_value_double(env, args[3], &endVal);
        napi_get_value_double(env, args[4], &duration);

        std::string easingStr = "easeInOut";
        if (argc > 5)
        {
            napi_get_value_string_utf8(env, args[5], nullptr, 0, &str_len);
            char *easing_buf = new char[str_len + 1];
            napi_get_value_string_utf8(env, args[5], easing_buf, str_len + 1, &str_len);
            easingStr = std::string(easing_buf);
            delete[] easing_buf;
        }

        auto comp = components[id];
        if (comp)
        {
            AnimationProperty prop = AnimationProperty::Opacity;
            if (propertyStr == "translateX")
                prop = AnimationProperty::TranslateX;
            else if (propertyStr == "translateY")
                prop = AnimationProperty::TranslateY;
            else if (propertyStr == "scaleX")
                prop = AnimationProperty::ScaleX;
            else if (propertyStr == "scaleY")
                prop = AnimationProperty::ScaleY;
            else if (propertyStr == "rotate")
                prop = AnimationProperty::Rotate;
            else if (propertyStr == "opacity")
                prop = AnimationProperty::Opacity;
            else if (propertyStr == "width")
                prop = AnimationProperty::Width;
            else if (propertyStr == "height")
                prop = AnimationProperty::Height;
            else if (propertyStr == "borderRadius")
                prop = AnimationProperty::BorderRadius;

            EasingType easing = EasingType::EaseInOut;
            if (easingStr == "linear")
                easing = EasingType::Linear;
            else if (easingStr == "easeIn")
                easing = EasingType::EaseIn;
            else if (easingStr == "easeOut")
                easing = EasingType::EaseOut;
            else if (easingStr == "easeInOut")
                easing = EasingType::EaseInOut;
            else if (easingStr == "easeInCubic")
                easing = EasingType::EaseInCubic;
            else if (easingStr == "easeOutCubic")
                easing = EasingType::EaseOutCubic;
            else if (easingStr == "easeInOutCubic")
                easing = EasingType::EaseInOutCubic;
            else if (easingStr == "easeInBack")
                easing = EasingType::EaseInBack;
            else if (easingStr == "easeOutBack")
                easing = EasingType::EaseOutBack;
            else if (easingStr == "easeInOutBack")
                easing = EasingType::EaseInOutBack;
            else if (easingStr == "spring")
                easing = EasingType::Spring;

            auto trans = std::make_shared<Transition>(prop, (float)startVal, (float)endVal, (float)duration, easing);
            Animator::getInstance().addTransition(comp.get(), trans);
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value SetComponentZIndex(napi_env env, napi_callback_info info)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t id, zIndex;
        napi_get_value_int32(env, args[0], &id);
        napi_get_value_int32(env, args[1], &zIndex);

        auto comp = components[id];
        if (comp)
        {
            comp->zIndex = zIndex;
        }

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value OnWindowEvent(napi_env env, napi_callback_info info,
                             std::vector<std::function<void(const WindowEvent &)>> &handlers)
    {
        size_t argc = 1;
        napi_value args[1];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        napi_ref callbackRef;
        napi_create_reference(env, args[0], 1, &callbackRef);

        handlers.push_back([env, callbackRef](const WindowEvent &e)
                           {
        napi_handle_scope scope;
        napi_open_handle_scope(env, &scope);

        napi_value callback;
        napi_get_reference_value(env, callbackRef, &callback);

        napi_value global;
        napi_get_global(env, &global);

        napi_value eventObj;
        napi_create_object(env, &eventObj);

        napi_value title, appId, pid, x, y, w, h;
        napi_create_string_utf8(env, e.title.c_str(), e.title.length(), &title);
        napi_create_string_utf8(env, e.appId.c_str(), e.appId.length(), &appId);
        napi_create_int32(env, (int32_t)e.appPID, &pid);
        napi_create_double(env, e.bounds.x, &x);
        napi_create_double(env, e.bounds.y, &y);
        napi_create_double(env, e.bounds.width, &w);
        napi_create_double(env, e.bounds.height, &h);

        napi_set_named_property(env, eventObj, "title", title);
        napi_set_named_property(env, eventObj, "appId", appId);
        napi_set_named_property(env, eventObj, "pid", pid);
        napi_set_named_property(env, eventObj, "x", x);
        napi_set_named_property(env, eventObj, "y", y);
        napi_set_named_property(env, eventObj, "width", w);
        napi_set_named_property(env, eventObj, "height", h);

        napi_call_function(env, global, callback, 1, &eventObj, nullptr);
        napi_close_handle_scope(env, scope); });

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value OnWindowOpen(napi_env env, napi_callback_info info)
    {
        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        return OnWindowEvent(env, info, appWindow->getWindowManager()->onWindowOpen);
    }
    napi_value OnWindowClose(napi_env env, napi_callback_info info)
    {
        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        return OnWindowEvent(env, info, appWindow->getWindowManager()->onWindowClose);
    }
    napi_value OnWindowFocus(napi_env env, napi_callback_info info)
    {
        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        return OnWindowEvent(env, info, appWindow->getWindowManager()->onWindowFocus);
    }
    napi_value OnWindowUnfocus(napi_env env, napi_callback_info info)
    {
        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        return OnWindowEvent(env, info, appWindow->getWindowManager()->onWindowUnfocus);
    }
    napi_value OnWindowMaximize(napi_env env, napi_callback_info info)
    {
        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        return OnWindowEvent(env, info, appWindow->getWindowManager()->onWindowMaximize);
    }
    napi_value OnWindowMinimize(napi_env env, napi_callback_info info)
    {
        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        return OnWindowEvent(env, info, appWindow->getWindowManager()->onWindowMinimize);
    }
    napi_value OnWindowResize(napi_env env, napi_callback_info info)
    {
        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        return OnWindowEvent(env, info, appWindow->getWindowManager()->onWindowResize);
    }

    napi_value OnWindowEventById(napi_env env, napi_callback_info info,
                                 const std::string &eventType)
    {
        size_t argc = 2;
        napi_value args[2];
        napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

        int32_t windowIndex;
        napi_get_value_int32(env, args[0], &windowIndex);

        napi_ref callbackRef;
        napi_create_reference(env, args[1], 1, &callbackRef);

        if (!appWindow)
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }
        WindowManager *wm = appWindow->getWindowManager();
        if (!wm || windowIndex < 0 || windowIndex >= (int)wm->windows.size())
        {
            napi_value r;
            napi_get_undefined(env, &r);
            return r;
        }

        auto handler = [env, callbackRef](const WindowEvent &e)
        {
            napi_handle_scope scope;
            napi_open_handle_scope(env, &scope);
            napi_value callback;
            napi_get_reference_value(env, callbackRef, &callback);
            napi_value global;
            napi_get_global(env, &global);
            napi_value eventObj;
            napi_create_object(env, &eventObj);
            napi_value title, appId, pid, x, y, w, h;
            napi_create_string_utf8(env, e.title.c_str(), e.title.length(), &title);
            napi_create_string_utf8(env, e.appId.c_str(), e.appId.length(), &appId);
            napi_create_int32(env, (int32_t)e.appPID, &pid);
            napi_create_double(env, e.bounds.x, &x);
            napi_create_double(env, e.bounds.y, &y);
            napi_create_double(env, e.bounds.width, &w);
            napi_create_double(env, e.bounds.height, &h);
            napi_set_named_property(env, eventObj, "title", title);
            napi_set_named_property(env, eventObj, "appId", appId);
            napi_set_named_property(env, eventObj, "pid", pid);
            napi_set_named_property(env, eventObj, "x", x);
            napi_set_named_property(env, eventObj, "y", y);
            napi_set_named_property(env, eventObj, "width", w);
            napi_set_named_property(env, eventObj, "height", h);
            napi_call_function(env, global, callback, 1, &eventObj, nullptr);
            napi_close_handle_scope(env, scope);
        };

        auto &win = wm->windows[windowIndex];
        if (eventType == "open")
            win.onOpen.push_back(handler);
        else if (eventType == "close")
            win.onClose.push_back(handler);
        else if (eventType == "focus")
            win.onFocus.push_back(handler);
        else if (eventType == "unfocus")
            win.onUnfocus.push_back(handler);
        else if (eventType == "maximize")
            win.onMaximize.push_back(handler);
        else if (eventType == "minimize")
            win.onMinimize.push_back(handler);
        else if (eventType == "resize")
            win.onResize.push_back(handler);

        napi_value result;
        napi_get_undefined(env, &result);
        return result;
    }

    napi_value OnWindowOpenById(napi_env env, napi_callback_info info) { return OnWindowEventById(env, info, "open"); }
    napi_value OnWindowCloseById(napi_env env, napi_callback_info info) { return OnWindowEventById(env, info, "close"); }
    napi_value OnWindowFocusById(napi_env env, napi_callback_info info) { return OnWindowEventById(env, info, "focus"); }
    napi_value OnWindowUnfocusById(napi_env env, napi_callback_info info) { return OnWindowEventById(env, info, "unfocus"); }
    napi_value OnWindowMaximizeById(napi_env env, napi_callback_info info) { return OnWindowEventById(env, info, "maximize"); }
    napi_value OnWindowMinimizeById(napi_env env, napi_callback_info info) { return OnWindowEventById(env, info, "minimize"); }
    napi_value OnWindowResizeById(napi_env env, napi_callback_info info) { return OnWindowEventById(env, info, "resize"); }

    napi_value Init(napi_env env, napi_value exports)
    {
        napi_property_descriptor desc[] = {
            {"animate", nullptr, AnimateComponent, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setZIndex", nullptr, SetComponentZIndex, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createWindow", nullptr, CreateGlassUIWindow, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createAppWindow", nullptr, CreateAppWindow, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createButton", nullptr, CreateButton, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setButtonOnClick", nullptr, SetButtonOnClick, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createView", nullptr, CreateView, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createImage", nullptr, CreateImage, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createTextBox", nullptr, CreateTextBox, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createText", nullptr, CreateText, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createMenu", nullptr, CreateMenu, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"addMenuItem", nullptr, AddMenuItem, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuItemIcon", nullptr, SetMenuItemIcon, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuItemChecked", nullptr, SetMenuItemChecked, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuIconsVisible", nullptr, SetMenuIconsVisible, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuCheckIconsVisible", nullptr, SetMenuCheckIconsVisible, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuIconSize", nullptr, SetMenuIconSize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuCheckIconSize", nullptr, SetMenuCheckIconSize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuIconMargin", nullptr, SetMenuIconMargin, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuCheckIconMargin", nullptr, SetMenuCheckIconMargin, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"addMenuSubmenu", nullptr, AddMenuSubmenu, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"addMenuSeparator", nullptr, AddMenuSeparator, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"showMenu", nullptr, ShowMenu, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"hideMenu", nullptr, HideMenu, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuFont", nullptr, SetMenuFont, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createDropdown", nullptr, CreateDropdown, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"addDropdownOption", nullptr, AddDropdownOption, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setDropdownSelectedIndex", nullptr, SetDropdownSelectedIndex, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setDropdownSelectedValue", nullptr, SetDropdownSelectedValue, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getDropdownSelectedIndex", nullptr, GetDropdownSelectedIndex, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getDropdownSelectedValue", nullptr, GetDropdownSelectedValue, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setDropdownPlaceholder", nullptr, SetDropdownPlaceholder, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setDropdownOnChange", nullptr, SetDropdownOnChange, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setDropdownFont", nullptr, SetDropdownFont, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setDropdownWindowSize", nullptr, SetDropdownWindowSize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setComponentStyle", nullptr, SetComponentStyle, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setButtonText", nullptr, SetButtonText, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setImageSource", nullptr, SetImageSource, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setText", nullptr, SetText, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setHoverStyle", nullptr, SetHoverStyle, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setPressedStyle", nullptr, SetPressedStyle, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"addChild", nullptr, AddChild, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"render", nullptr, Render, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"shouldClose", nullptr, ShouldClose, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"parseCSS", nullptr, ParseCSS, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"parseCSSFile", nullptr, ParseCSSFile, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"applyCSS", nullptr, ApplyCSS, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"runEventLoop", nullptr, RunEventLoop, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setGradient", nullptr, SetGradient, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setFilter", nullptr, SetFilter, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setBackdropFilter", nullptr, SetBackdropFilter, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setUpdateCallback", nullptr, SetUpdateCallback, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"loadAudio", nullptr, LoadAudio, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"unloadAudio", nullptr, UnloadAudio, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playAudio", nullptr, PlayAudio, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"pauseAudio", nullptr, PauseAudio, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"stopAudio", nullptr, StopAudio, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"isAudioPlaying", nullptr, IsAudioPlaying, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getAudioCurrentTime", nullptr, GetAudioCurrentTime, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setAudioCurrentTime", nullptr, SetAudioCurrentTime, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getAudioDuration", nullptr, GetAudioDuration, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setAudioLoop", nullptr, SetAudioLoop, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setAudioSpeed", nullptr, SetAudioSpeed, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setAudioVolume", nullptr, SetAudioVolume, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getSystemVolume", nullptr, GetSystemVolume, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSystemVolume", nullptr, SetSystemVolume, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getOutputDevices", nullptr, GetOutputDevices, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getInputDevices", nullptr, GetInputDevices, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createSidebar", nullptr, CreateSidebar, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"addSidebarItem", nullptr, AddSidebarItem, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSidebarActiveItem", nullptr, SetSidebarActiveItem, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getSidebarActiveItem", nullptr, GetSidebarActiveItem, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSidebarWidth", nullptr, SetSidebarWidth, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSidebarItemIcon", nullptr, SetSidebarItemIcon, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSidebarFont", nullptr, SetSidebarFont, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSidebarItemHeight", nullptr, SetSidebarItemHeight, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSidebarIconSize", nullptr, SetSidebarIconSize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setSidebarIconMargin", nullptr, SetSidebarIconMargin, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createManagedWindow", nullptr, CreateManagedWindow, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setWindowZOrder", nullptr, SetWindowZOrder, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getWindowCount", nullptr, GetWindowCount, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createMenuBar", nullptr, CreateMenuBar, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"addMenuBarMenu", nullptr, AddMenuBarMenu, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setVisibility", nullptr, SetVisibility, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuBarFont", nullptr, SetMenuBarFont, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setMenuBarWindowSize", nullptr, SetMenuBarWindowSize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"createProgressBar", nullptr, CreateProgressBar, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setProgressBarValue", nullptr, SetProgressBarValue, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"getProgressBarValue", nullptr, GetProgressBarValue, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"updateProgressBar", nullptr, UpdateProgressBar, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"setOutputDevice", nullptr, SetOutputDevice, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowOpen", nullptr, OnWindowOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowClose", nullptr, OnWindowClose, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowFocus", nullptr, OnWindowFocus, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowUnfocus", nullptr, OnWindowUnfocus, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowMaximize", nullptr, OnWindowMaximize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowMinimize", nullptr, OnWindowMinimize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowResize", nullptr, OnWindowResize, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowOpenById", nullptr, OnWindowOpenById, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowCloseById", nullptr, OnWindowCloseById, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowFocusById", nullptr, OnWindowFocusById, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowUnfocusById", nullptr, OnWindowUnfocusById, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowMaximizeById", nullptr, OnWindowMaximizeById, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowMinimizeById", nullptr, OnWindowMinimizeById, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"onWindowResizeById", nullptr, OnWindowResizeById, nullptr, nullptr, nullptr, napi_default, nullptr},
        };

        napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
        return exports;
    }

}

NAPI_MODULE(NODE_GYP_MODULE_NAME, GlassUI::Init)