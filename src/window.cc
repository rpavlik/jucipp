#include "window.h"
#include "logging.h"
#include "singletons.h"
#include "config.h"
//#include "api.h"
#include "dialogs.h"

#include <iostream> //TODO: remove
using namespace std; //TODO: remove

namespace sigc {
#ifndef SIGC_FUNCTORS_DEDUCE_RESULT_TYPE_WITH_DECLTYPE
  template <typename Functor>
  struct functor_trait<Functor, false> {
    typedef decltype (::sigc::mem_fun(std::declval<Functor&>(),
                                      &Functor::operator())) _intermediate;
    typedef typename _intermediate::result_type result_type;
    typedef Functor functor_type;
  };
#else
  SIGC_FUNCTORS_DEDUCE_RESULT_TYPE_WITH_DECLTYPE
#endif
}

void Window::generate_keybindings() {
  for (auto &i : Singleton::Config::window()->keybindings) {
    auto key = i.second.get_value<std::string>();
    menu.key_map[i.first] = key;
  }
}

Window::Window() : box(Gtk::ORIENTATION_VERTICAL), compiling(false) {
  JDEBUG("start");
  set_title("juCi++");
  set_events(Gdk::POINTER_MOTION_MASK|Gdk::FOCUS_CHANGE_MASK|Gdk::SCROLL_MASK);
  configure();
  set_default_size(Singleton::Config::window()->default_size.first, Singleton::Config::window()->default_size.second);
  
  //PluginApi(&this->notebook, &this->menu);
  
  add(box);

  //TODO: Do not use deprecated ui_manager? And make menu shortcuts update when config.json is saved (in configure())
  generate_keybindings();
  create_menu();
  menu.build();
  add_accel_group(menu.ui_manager->get_accel_group());
  box.pack_start(menu.get_widget(), Gtk::PACK_SHRINK);

  directory_and_notebook_panes.pack1(*Singleton::directories(), Gtk::SHRINK);
  notebook_vbox.pack_start(notebook);
  notebook_vbox.pack_end(entry_box, Gtk::PACK_SHRINK);
  directory_and_notebook_panes.pack2(notebook_vbox, Gtk::SHRINK);
  directory_and_notebook_panes.set_position(static_cast<int>(0.2*Singleton::Config::window()->default_size.first));
  vpaned.set_position(static_cast<int>(0.75*Singleton::Config::window()->default_size.second));
  vpaned.pack1(directory_and_notebook_panes, true, false);
  
  terminal_scrolled_window.add(*Singleton::terminal());
  terminal_vbox.pack_start(terminal_scrolled_window);
    
  info_and_status_hbox.pack_start(*Singleton::info(), Gtk::PACK_SHRINK);
  info_and_status_hbox.pack_end(*Singleton::status(), Gtk::PACK_SHRINK);
  terminal_vbox.pack_end(info_and_status_hbox, Gtk::PACK_SHRINK);
  vpaned.pack2(terminal_vbox, true, true);
  
  box.pack_end(vpaned);
  show_all_children();

  Singleton::directories()->on_row_activated=[this](const std::string &file) {
    notebook.open(file);
  };

  //Scroll to end of terminal whenever info is printed
  Singleton::terminal()->signal_size_allocate().connect([this](Gtk::Allocation& allocation){
    auto adjustment=terminal_scrolled_window.get_vadjustment();
    adjustment->set_value(adjustment->get_upper()-adjustment->get_page_size());
    Singleton::terminal()->queue_draw();
  });

  entry_box.signal_show().connect([this](){
    box.set_focus_chain({&vpaned});
    vpaned.set_focus_chain({&directory_and_notebook_panes});
    directory_and_notebook_panes.set_focus_chain({&notebook_vbox});
    notebook_vbox.set_focus_chain({&entry_box});
  });
  entry_box.signal_hide().connect([this](){
    box.unset_focus_chain();
    vpaned.unset_focus_chain();
    directory_and_notebook_panes.unset_focus_chain();
    notebook_vbox.unset_focus_chain();
  });
  entry_box.signal_hide().connect([this]() {
    if(notebook.get_current_page()!=-1) {
      notebook.get_current_view()->grab_focus();
    }
  });

  notebook.signal_switch_page().connect([this](Gtk::Widget* page, guint page_num) {
    if(notebook.get_current_page()!=-1) {
      if(search_entry_shown && entry_box.labels.size()>0) {
        notebook.get_current_view()->update_search_occurrences=[this](int number){
          entry_box.labels.begin()->update(0, std::to_string(number));
        };
        notebook.get_current_view()->search_highlight(last_search, case_sensitive_search, regex_search);
      }

      if(auto menu_item=dynamic_cast<Gtk::MenuItem*>(menu.ui_manager->get_widget("/MenuBar/SourceMenu/SourceGotoDeclaration")))
        menu_item->set_sensitive((bool)notebook.get_current_view()->get_declaration_location);

      if(auto menu_item=dynamic_cast<Gtk::MenuItem*>(menu.ui_manager->get_widget("/MenuBar/SourceMenu/SourceGotoMethod")))
        menu_item->set_sensitive((bool)notebook.get_current_view()->goto_method);

      if(auto menu_item=dynamic_cast<Gtk::MenuItem*>(menu.ui_manager->get_widget("/MenuBar/SourceMenu/SourceRename")))
        menu_item->set_sensitive((bool)notebook.get_current_view()->rename_similar_tokens);
      
      if(auto menu_item=dynamic_cast<Gtk::MenuItem*>(menu.ui_manager->get_widget("/MenuBar/SourceMenu/SourceGotoNextDiagnostic")))
        menu_item->set_sensitive((bool)notebook.get_current_view()->goto_next_diagnostic);
      
      if(auto menu_item=dynamic_cast<Gtk::MenuItem*>(menu.ui_manager->get_widget("/MenuBar/SourceMenu/SourceApplyFixIts")))
        menu_item->set_sensitive((bool)notebook.get_current_view()->apply_fix_its);
      
      if(auto menu_item=dynamic_cast<Gtk::MenuItem*>(menu.ui_manager->get_widget("/MenuBar/SourceMenu/SourceFindDocumentation")))
        menu_item->set_sensitive((bool)notebook.get_current_view()->get_token_data);
    
      Singleton::directories()->select(notebook.get_current_view()->file_path);
      
      if(auto source_view=dynamic_cast<Source::ClangView*>(notebook.get_current_view())) {
        if(source_view->reparse_needed) {
          source_view->start_reparse();
          source_view->reparse_needed=false;
        }
      }
      
      notebook.get_current_view()->set_status(notebook.get_current_view()->status);
      notebook.get_current_view()->set_info(notebook.get_current_view()->info);
    }
  });
  notebook.signal_page_removed().connect([this](Gtk::Widget* page, guint page_num) {
    entry_box.hide();
  });
  
  about.signal_response().connect([this](int d){
    about.hide();
  });
  
  about.set_version(Singleton::Config::window()->version);
  about.set_authors({"(in order of appearance)",
                     "Ted Johan Kristoffersen", 
                     "Jørgen Lien Sellæg",
                     "Geir Morten Larsen",
                     "Ole Christian Eidheim"});
  about.set_name("About juCi++");
  about.set_program_name("juCi++");
  about.set_comments("This is an open source IDE with high-end features to make your programming experience juicy");
  about.set_license_type(Gtk::License::LICENSE_MIT_X11);
  about.set_transient_for(*this);
  JDEBUG("end");
} // Window constructor

void Window::configure() {
  MainConfig(); // Read the configs here
  auto style_context = Gtk::StyleContext::create();
  auto screen = Gdk::Screen::get_default();
  auto css_provider = Gtk::CssProvider::get_named(Singleton::Config::window()->theme_name, Singleton::Config::window()->theme_variant);
  //TODO: add check if theme exists, or else write error to Singleton::terminal()
  style_context->add_provider_for_screen(screen, css_provider, GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
}

void Window::create_menu() {
  menu.action_group->add(Gtk::Action::create("FileQuit", "Quit juCi++"), Gtk::AccelKey(menu.key_map["quit"]), [this]() {
    hide();
  });
  menu.action_group->add(Gtk::Action::create("FileNewFile", "New File"), Gtk::AccelKey(menu.key_map["new_file"]), [this]() {
      boost::filesystem::path path = Dialog::new_file();
      if(path!="") {
        if(boost::filesystem::exists(path)) {
          Singleton::terminal()->print("Error: "+path.string()+" already exists.\n");
        }
        else {
          if(juci::filesystem::write(path)) {
            if(Singleton::directories()->current_path!="")
              Singleton::directories()->update();
            notebook.open(path.string());
            Singleton::terminal()->print("New file "+path.string()+" created.\n");
          }
          else
            Singleton::terminal()->print("Error: could not create new file "+path.string()+".\n");
        }
      }
  });
  menu.action_group->add(Gtk::Action::create("FileNewFolder", "New Folder"), Gtk::AccelKey(menu.key_map["new_folder"]), [this]() {
    auto time_now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      boost::filesystem::path path = Dialog::new_folder();
      if(path!="" && boost::filesystem::exists(path)) {
        if(boost::filesystem::last_write_time(path)>=time_now) {
          if(Singleton::directories()->current_path!="")
            Singleton::directories()->update();
          Singleton::terminal()->print("New folder "+path.string()+" created.\n");
        }
        else
          Singleton::terminal()->print("Error: "+path.string()+" already exists.\n");
      }
      Singleton::directories()->select(path);
  });
  menu.action_group->add(Gtk::Action::create("FileNewProject", "New Project"));
  menu.action_group->add(Gtk::Action::create("FileNewProjectCpp", "C++"), [this]() {
      boost::filesystem::path project_path = Dialog::new_folder();
      if(project_path=="")
        return;
      auto project_name=project_path.filename().string();
      for(size_t c=0;c<project_name.size();c++) {
        if(project_name[c]==' ')
          project_name[c]='_';
      }
      auto cmakelists_path=project_path;
      cmakelists_path+="/CMakeLists.txt";
      auto cpp_main_path=project_path;
      cpp_main_path+="/main.cpp";
      if(boost::filesystem::exists(cmakelists_path)) {
        Singleton::terminal()->print("Error: "+cmakelists_path.string()+" already exists.\n");
        return;
      }
      if(boost::filesystem::exists(cpp_main_path)) {
        Singleton::terminal()->print("Error: "+cpp_main_path.string()+" already exists.\n");
        return;
      }
      std::string cmakelists="cmake_minimum_required(VERSION 2.8)\n\nproject("+project_name+")\n\nset(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -std=c++1y -Wall\")\n\nadd_executable("+project_name+" main.cpp)\n";
      std::string cpp_main="#include <iostream>\n\nusing namespace std;\n\nint main() {\n  cout << \"Hello World!\" << endl;\n\n  return 0;\n}\n";
      if(juci::filesystem::write(cmakelists_path, cmakelists) && juci::filesystem::write(cpp_main_path, cpp_main)) {
        Singleton::directories()->open(project_path);
        notebook.open(cpp_main_path);
        Singleton::terminal()->print("C++ project "+project_name+" created.\n");
      }
      else
        Singleton::terminal()->print("Error: Could not create project "+project_path.string()+"\n");
  });
  menu.action_group->add(Gtk::Action::create("FileOpenFile", "Open File"), Gtk::AccelKey(menu.key_map["open_file"]), [this]() {
      notebook.open(Dialog::select_file());
  });
  menu.action_group->add(Gtk::Action::create("FileOpenFolder", "Open Folder"), Gtk::AccelKey(menu.key_map["open_folder"]), [this]() {
    auto path = Dialog::select_folder();
    if (path!="" && boost::filesystem::exists(path))
      Singleton::directories()->open(path);
  });
  menu.action_group->add(Gtk::Action::create("FileSaveAs", "Save As"), Gtk::AccelKey(menu.key_map["save_as"]), [this]() {
    auto path = Dialog::save_file();
    if(path!="") {
      std::ofstream file(path);
      if(file) {
        file << notebook.get_current_view()->get_buffer()->get_text();
        file.close();
        if(Singleton::directories()->current_path!="")
          Singleton::directories()->update();
        notebook.open(path);
        Singleton::terminal()->print("File saved to: " + notebook.get_current_view()->file_path.string()+"\n");
      }
      else
        Singleton::terminal()->print("Error saving file\n");
    }
});
  menu.action_group->add(Gtk::Action::create("Preferences", "Preferences..."), Gtk::AccelKey(menu.key_map["preferences"]), [this]() {
    notebook.open(Singleton::config_dir()+"config.json");
  });

  menu.action_group->add(Gtk::Action::create("FileSave", "Save"), Gtk::AccelKey(menu.key_map["save"]), [this]() {
    if(notebook.save_current()) {
      if(notebook.get_current_page()!=-1) {
        if(notebook.get_current_view()->file_path==Singleton::config_dir()+"config.json") {
          configure();
          for(int c=0;c<notebook.size();c++) {
            notebook.get_view(c)->configure();
            notebook.configure(c);
          }
        }
      }
    }
  });

  menu.action_group->add(Gtk::Action::create("EditCopy", "Copy"), Gtk::AccelKey(menu.key_map["edit_copy"]), [this]() {
    auto widget=get_focus();
    if(auto entry=dynamic_cast<Gtk::Entry*>(widget))
      entry->copy_clipboard();
    else if(auto text_view=dynamic_cast<Gtk::TextView*>(widget))
      text_view->get_buffer()->copy_clipboard(Gtk::Clipboard::get());
  });
  menu.action_group->add(Gtk::Action::create("EditCut", "Cut"), Gtk::AccelKey(menu.key_map["edit_cut"]), [this]() {
    auto widget=get_focus();
    if(auto entry=dynamic_cast<Gtk::Entry*>(widget))
      entry->cut_clipboard();
    else if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->get_buffer()->cut_clipboard(Gtk::Clipboard::get());
  });
  menu.action_group->add(Gtk::Action::create("EditPaste", "Paste"), Gtk::AccelKey(menu.key_map["edit_paste"]), [this]() {
    auto widget=get_focus();
    if(auto entry=dynamic_cast<Gtk::Entry*>(widget))
      entry->paste_clipboard();
    else if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->paste();
  });
  menu.action_group->add(Gtk::Action::create("EditFind", "Find"), Gtk::AccelKey(menu.key_map["edit_find"]), [this]() {
    search_and_replace_entry();
  });
  menu.action_group->add(Gtk::Action::create("EditUndo", "Undo"), Gtk::AccelKey(menu.key_map["edit_undo"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      auto undo_manager = notebook.get_current_view()->get_source_buffer()->get_undo_manager();
      if (undo_manager->can_undo()) {
        undo_manager->undo();
        notebook.get_current_view()->scroll_to(notebook.get_current_view()->get_buffer()->get_insert());
      }
    }
  });
  menu.action_group->add(Gtk::Action::create("EditRedo", "Redo"), Gtk::AccelKey(menu.key_map["edit_redo"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      auto undo_manager = notebook.get_current_view()->get_source_buffer()->get_undo_manager();
      if(undo_manager->can_redo()) {
        undo_manager->redo();
        notebook.get_current_view()->scroll_to(notebook.get_current_view()->get_buffer()->get_insert());
      }
    }
  });

  menu.action_group->add(Gtk::Action::create("SourceSpellCheck", "Spell Check"));
  menu.action_group->add(Gtk::Action::create("SourceSpellCheckBuffer", "Spell Check Buffer"), Gtk::AccelKey(menu.key_map["source_spellcheck"]), [this]() {
    if(notebook.get_current_page()!=-1)
        notebook.get_current_view()->spellcheck();
  });
  menu.action_group->add(Gtk::Action::create("SourceSpellCheckClear", "Clear Spelling Errors"), Gtk::AccelKey(menu.key_map["source_spellcheck_clear"]), [this]() {
    if(notebook.get_current_page()!=-1)
        notebook.get_current_view()->remove_spellcheck_errors();
  });
  menu.action_group->add(Gtk::Action::create("SourceSpellCheckNextError", "Go to next Spelling Error"), Gtk::AccelKey(menu.key_map["source_spellcheck_next_error"]), [this]() {
    if(notebook.get_current_page()!=-1)
        notebook.get_current_view()->goto_next_spellcheck_error();
  });
  menu.action_group->add(Gtk::Action::create("SourceIndentation", "Indentation"));
  menu.action_group->add(Gtk::Action::create("SourceIndentationSetBufferTab", "Set Current Buffer Tab"), Gtk::AccelKey(menu.key_map["source_indentation_set_buffer_tab"]), [this]() {
    set_tab_entry();
  });
  menu.action_group->add(Gtk::Action::create("SourceIndentationAutoIndentBuffer", "Auto-Indent Current Buffer"), Gtk::AccelKey(menu.key_map["source_indentation_auto_indent_buffer"]), [this]() {
    Singleton::terminal()->print("Auto-Indent Current Buffer will soon be implemented.\n");
  });
  menu.action_group->add(Gtk::Action::create("SourceGotoLine", "Go to Line"), Gtk::AccelKey(menu.key_map["source_goto_line"]), [this]() {
    goto_line_entry();
  });
  menu.action_group->add(Gtk::Action::create("SourceCenterCursor", "Center Cursor"), Gtk::AccelKey(menu.key_map["source_center_cursor"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      while(gtk_events_pending())
        gtk_main_iteration();
      if(notebook.get_current_page()!=-1)
        notebook.get_current_view()->scroll_to(notebook.get_current_view()->get_buffer()->get_insert(), 0.0, 1.0, 0.5);
    }
  });
  menu.action_group->add(Gtk::Action::create("SourceGotoDeclaration", "Go to Declaration"), Gtk::AccelKey(menu.key_map["source_goto_declaration"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      if(notebook.get_current_view()->get_declaration_location) {
        auto location=notebook.get_current_view()->get_declaration_location();
        if(location.first.size()>0) {
          notebook.open(location.first);
          notebook.get_current_view()->get_buffer()->place_cursor(notebook.get_current_view()->get_buffer()->get_iter_at_line_index(location.second.line-1, location.second.index-1));
          while(gtk_events_pending())
            gtk_main_iteration();
          if(notebook.get_current_page()!=-1)
            notebook.get_current_view()->scroll_to(notebook.get_current_view()->get_buffer()->get_insert(), 0.0, 1.0, 0.5);
        }
      }
    }
  });
  menu.action_group->add(Gtk::Action::create("SourceGotoMethod", "Go to Method"), Gtk::AccelKey(menu.key_map["source_goto_method"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      if(notebook.get_current_view()->goto_method) {
        notebook.get_current_view()->goto_method();
      }
    }
  });
  menu.action_group->add(Gtk::Action::create("SourceRename", "Rename"), Gtk::AccelKey(menu.key_map["source_rename"]), [this]() {
    rename_token_entry();
  });
  menu.action_group->add(Gtk::Action::create("SourceFindDocumentation", "Find Documentation"), Gtk::AccelKey(menu.key_map["source_find_documentation"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      if(notebook.get_current_view()->get_token_data) {
        auto data=notebook.get_current_view()->get_token_data();        
        if(data.size()>0) {
          auto documentation_search=Singleton::Config::source()->documentation_searches.find(data[0]);
          if(documentation_search!=Singleton::Config::source()->documentation_searches.end()) {
            std::string token_query;
            for(size_t c=1;c<data.size();c++) {
              if(data[c].size()>0) {
                if(token_query.size()>0)
                  token_query+=documentation_search->second.separator;
                token_query+=data[c];
              }
            }
            if(token_query.size()>0) {
              std::unordered_map<std::string, std::string>::iterator query;
              if(data[1].size()>0)
                query=documentation_search->second.queries.find(data[1]);
              else
                query=documentation_search->second.queries.find("@empty");
              if(query==documentation_search->second.queries.end())
                query=documentation_search->second.queries.find("@any");
              
              if(query!=documentation_search->second.queries.end()) {
                std::string uri=query->second+token_query;
#ifdef __APPLE__
                Singleton::terminal()->execute("open \""+uri+"\"");
#else
                GError* error=NULL;
                gtk_show_uri(NULL, uri.c_str(), GDK_CURRENT_TIME, &error);
                g_clear_error(&error);
#endif
              }
            }
          }
        }
      }
    }
  });
  menu.action_group->add(Gtk::Action::create("SourceGotoNextDiagnostic", "Go to next Diagnostic"), Gtk::AccelKey(menu.key_map["source_goto_next_diagnostic"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      if(notebook.get_current_view()->goto_next_diagnostic) {
        notebook.get_current_view()->goto_next_diagnostic();
      }
    }
  });
  menu.action_group->add(Gtk::Action::create("SourceApplyFixIts", "Apply Fix-Its"), Gtk::AccelKey(menu.key_map["source_apply_fix_its"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      if(notebook.get_current_view()->apply_fix_its) {
        notebook.get_current_view()->apply_fix_its();
      }
    }
  });

  menu.action_group->add(Gtk::Action::create("ProjectCompileAndRun", "Compile and Run"), Gtk::AccelKey(menu.key_map["compile_and_run"]), [this]() {
    if(notebook.get_current_page()==-1 || compiling)
      return;
    CMake cmake(notebook.get_current_view()->file_path);
    auto executables = cmake.get_functions_parameters("add_executable");
    boost::filesystem::path executable_path;
    if(executables.size()>0 && executables[0].second.size()>0) {
      executable_path=executables[0].first.parent_path();
      executable_path+="/"+executables[0].second[0];
    }
    if(cmake.project_path!="") {
      if(executable_path!="") {
        compiling=true;
        Singleton::terminal()->print("Compiling and running "+executable_path.string()+"\n");
        auto project_path=cmake.project_path;
        Singleton::terminal()->async_execute(Singleton::Config::terminal()->make_command, cmake.project_path, [this, executable_path, project_path](int exit_code){
          compiling=false;
          if(exit_code==EXIT_SUCCESS) {
            auto executable_path_spaces_fixed=executable_path.string();
            char last_char=0;
            for(size_t c=0;c<executable_path_spaces_fixed.size();c++) {
              if(last_char!='\\' && executable_path_spaces_fixed[c]==' ') {
                executable_path_spaces_fixed.insert(c, "\\");
                c++;
              }
              last_char=executable_path_spaces_fixed[c];
            }
            Singleton::terminal()->async_execute(executable_path_spaces_fixed, project_path, [this, executable_path](int exit_code){
              Singleton::terminal()->async_print(executable_path.string()+" returned: "+std::to_string(exit_code)+'\n');
            });
          }
        });
      }
      else {
        Singleton::terminal()->print("Could not find add_executable in the following paths:\n");
        for(auto &path: cmake.paths)
          Singleton::terminal()->print("  "+path.string()+"\n");
      }
    }
  });
  menu.action_group->add(Gtk::Action::create("ProjectCompile", "Compile"), Gtk::AccelKey(menu.key_map["compile"]), [this]() {
    if(notebook.get_current_page()==-1 || compiling)
      return;
    CMake cmake(notebook.get_current_view()->file_path);
    if(cmake.project_path!="") {
      compiling=true;
      Singleton::terminal()->print("Compiling project "+cmake.project_path.string()+"\n");
      Singleton::terminal()->async_execute(Singleton::Config::terminal()->make_command, cmake.project_path, [this](int exit_code){
        compiling=false;
      });
    }
  });
  menu.action_group->add(Gtk::Action::create("ProjectRunCommand", "Run Command"), Gtk::AccelKey(menu.key_map["run_command"]), [this]() {
    entry_box.clear();
    entry_box.labels.emplace_back();
    auto label_it=entry_box.labels.begin();
    label_it->update=[label_it](int state, const std::string& message){
      label_it->set_text("Run Command directory order: file project path, file directory, opened directory, current directory");
    };
    label_it->update(0, "");
    entry_box.entries.emplace_back(last_run_command, [this](const std::string& content){
      if(content!="") {
        last_run_command=content;
        boost::filesystem::path run_path;
        if(notebook.get_current_page()!=-1) {
          if(notebook.get_current_view()->project_path!="")
            run_path=notebook.get_current_view()->project_path;
          else
            run_path=notebook.get_current_view()->file_path.parent_path();
        }
        else
          run_path=Singleton::directories()->current_path;
        Singleton::terminal()->async_print("Running: "+content+'\n');

        Singleton::terminal()->async_execute(content, run_path, [this, content](int exit_code){
          Singleton::terminal()->async_print(content+" returned: "+std::to_string(exit_code)+'\n');
        });
      }
      entry_box.hide();
    });
    auto entry_it=entry_box.entries.begin();
    entry_it->set_placeholder_text("Command");
    entry_box.buttons.emplace_back("Run command", [this, entry_it](){
      entry_it->activate();
    });
    entry_box.show();
  });
  menu.action_group->add(Gtk::Action::create("ProjectKillLastRunning", "Kill Last Process"), Gtk::AccelKey(menu.key_map["kill_last_running"]), [this]() {
    Singleton::terminal()->kill_last_async_execute();
  });
  menu.action_group->add(Gtk::Action::create("ProjectForceKillLastRunning", "Force Kill Last Process"), Gtk::AccelKey(menu.key_map["force_kill_last_running"]), [this]() {
    Singleton::terminal()->kill_last_async_execute(true);
  });
  menu.action_group->add(Gtk::Action::create("WindowCloseTab", "Close Tab"), Gtk::AccelKey(menu.key_map["close_tab"]), [this]() {
    notebook.close_current_page();
    if(notebook.get_current_page()!=-1) {
      notebook.get_current_view()->set_status(notebook.get_current_view()->status);
      notebook.get_current_view()->set_info(notebook.get_current_view()->info);
    }
    else {
      Singleton::status()->set_text("");
      Singleton::info()->set_text("");
    }
  });
  menu.action_group->add(Gtk::Action::create("WindowNextTab", "Next Tab"), Gtk::AccelKey(menu.key_map["next_tab"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      notebook.open(notebook.get_view((notebook.get_current_page()+1)%notebook.size())->file_path);
    }
  });
  menu.action_group->add(Gtk::Action::create("WindowPreviousTab", "Previous Tab"), Gtk::AccelKey(menu.key_map["previous_tab"]), [this]() {
    if(notebook.get_current_page()!=-1) {
      int previous_page=notebook.get_current_page()-1;
      if(previous_page<0)
        previous_page=notebook.size()-1;
      notebook.open(notebook.get_view(previous_page)->file_path);
    }
  });
  menu.action_group->add(Gtk::Action::create("HelpAbout", "About"), [this] () {
    about.show();
    about.present();
  });
}

bool Window::on_key_press_event(GdkEventKey *event) {
  if(event->keyval==GDK_KEY_Escape) {
    entry_box.hide();
  }
#ifdef __APPLE__ //For Apple's Command-left, right, up, down keys
  else if((event->state & GDK_META_MASK)>0 && (event->state & GDK_MOD1_MASK)==0) {
    if(event->keyval==GDK_KEY_Left) {
      event->keyval=GDK_KEY_Home;
      event->state=event->state & GDK_SHIFT_MASK;
      event->hardware_keycode=115;
    }
    else if(event->keyval==GDK_KEY_Right) {
      event->keyval=GDK_KEY_End;
      event->state=event->state & GDK_SHIFT_MASK;
      event->hardware_keycode=119;
    }
    else if(event->keyval==GDK_KEY_Up) {
      event->keyval=GDK_KEY_Home;
      event->state=event->state & GDK_SHIFT_MASK;
      event->state+=GDK_CONTROL_MASK;
      event->hardware_keycode=115;
    }
    else if(event->keyval==GDK_KEY_Down) {
      event->keyval=GDK_KEY_End;
      event->state=event->state & GDK_SHIFT_MASK;
      event->state+=GDK_CONTROL_MASK;
      event->hardware_keycode=119;
    }
  }
#endif

  return Gtk::Window::on_key_press_event(event);
}

bool Window::on_delete_event (GdkEventAny *event) {
  hide();
  return true;
}

void Window::hide() {
  auto size=notebook.size();
  for(int c=0;c<size;c++) {
    if(!notebook.close_current_page())
      return;
  }
  Singleton::terminal()->kill_async_executes();
  Gtk::Window::hide();
}

void Window::search_and_replace_entry() {
  entry_box.clear();
  entry_box.labels.emplace_back();
  auto label_it=entry_box.labels.begin();
  label_it->update=[label_it](int state, const std::string& message){
    if(state==0) {
      try {
        auto number = stoi(message);
        if(number==0)
          label_it->set_text("");
        else if(number==1)
          label_it->set_text("1 result found");
        else if(number>1)
          label_it->set_text(message+" results found");
      }
      catch(const std::exception &e) {}
    }
  };
  entry_box.entries.emplace_back(last_search, [this](const std::string& content){
    if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->search_forward();
  });
  auto search_entry_it=entry_box.entries.begin();
  search_entry_it->set_placeholder_text("Find");
  if(notebook.get_current_page()!=-1) {
    notebook.get_current_view()->update_search_occurrences=[label_it](int number){
      label_it->update(0, std::to_string(number));
    };
    notebook.get_current_view()->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  }
  search_entry_it->signal_key_press_event().connect([this](GdkEventKey* event){
    if(event->keyval==GDK_KEY_Return && (event->state&GDK_SHIFT_MASK)>0) {
      if(notebook.get_current_page()!=-1)
        notebook.get_current_view()->search_backward();
    }
    return false;
  });
  search_entry_it->signal_changed().connect([this, search_entry_it](){
    last_search=search_entry_it->get_text();
    if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  });

  entry_box.entries.emplace_back(last_replace, [this](const std::string &content){
    if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->replace_forward(content);
  });
  auto replace_entry_it=entry_box.entries.begin();
  replace_entry_it++;
  replace_entry_it->set_placeholder_text("Replace");
  replace_entry_it->signal_key_press_event().connect([this, replace_entry_it](GdkEventKey* event){
    if(event->keyval==GDK_KEY_Return && (event->state&GDK_SHIFT_MASK)>0) {
      if(notebook.get_current_page()!=-1)
        notebook.get_current_view()->replace_backward(replace_entry_it->get_text());
    }
    return false;
  });
  replace_entry_it->signal_changed().connect([this, replace_entry_it](){
    last_replace=replace_entry_it->get_text();
  });

  entry_box.buttons.emplace_back("Replace all", [this, replace_entry_it](){
    if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->replace_all(replace_entry_it->get_text());
  });
  entry_box.toggle_buttons.emplace_back("Match case");
  entry_box.toggle_buttons.back().set_active(case_sensitive_search);
  entry_box.toggle_buttons.back().on_activate=[this, search_entry_it](){
    case_sensitive_search=!case_sensitive_search;
    if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  };
  entry_box.toggle_buttons.emplace_back("Use regex");
  entry_box.toggle_buttons.back().set_active(regex_search);
  entry_box.toggle_buttons.back().on_activate=[this, search_entry_it](){
    regex_search=!regex_search;
    if(notebook.get_current_page()!=-1)
      notebook.get_current_view()->search_highlight(search_entry_it->get_text(), case_sensitive_search, regex_search);
  };
  entry_box.signal_hide().connect([this]() {
    for(int c=0;c<notebook.size();c++) {
      notebook.get_view(c)->update_search_occurrences=nullptr;
      notebook.get_view(c)->search_highlight("", case_sensitive_search, regex_search);
    }
    search_entry_shown=false;
  });
  search_entry_shown=true;
  entry_box.show();
}

void Window::set_tab_entry() {
  entry_box.clear();
  if(notebook.get_current_page()!=-1) {
    auto tab_char_and_size=notebook.get_current_view()->get_tab_char_and_size();
    
    entry_box.labels.emplace_back();
    auto label_it=entry_box.labels.begin();
    
    entry_box.entries.emplace_back(std::to_string(tab_char_and_size.second));
    auto entry_tab_size_it=entry_box.entries.begin();
    entry_tab_size_it->set_placeholder_text("Tab size");
    
    char tab_char=tab_char_and_size.first;
    std::string tab_char_string;
    if(tab_char==' ')
      tab_char_string="space";
    else if(tab_char=='\t')
      tab_char_string="tab";
      
    entry_box.entries.emplace_back(tab_char_string);
    auto entry_tab_char_it=entry_box.entries.rbegin();
    entry_tab_char_it->set_placeholder_text("Tab char");
    
    const auto activate_function=[this, entry_tab_char_it, entry_tab_size_it, label_it](const std::string& content){
      if(notebook.get_current_page()!=-1) {
        char tab_char=0;
        unsigned tab_size=0;
        try {
          tab_size = static_cast<unsigned>(stoul(entry_tab_size_it->get_text()));
          std::string tab_char_string=entry_tab_char_it->get_text();
          std::transform(tab_char_string.begin(), tab_char_string.end(), tab_char_string.begin(), ::tolower);
          if(tab_char_string=="space")
            tab_char=' ';
          else if(tab_char_string=="tab")
            tab_char='\t';
        }
        catch(const std::exception &e) {}

        if(tab_char!=0 && tab_size>0) {
          notebook.get_current_view()->set_tab_char_and_size(tab_char, tab_size);
          entry_box.hide();
        }
        else {
          label_it->set_text("Tab size must be >0 and tab char set to either 'space' or 'tab'");
        }
      }
    };
    
    entry_tab_char_it->on_activate=activate_function;
    entry_tab_size_it->on_activate=activate_function;
    
    entry_box.buttons.emplace_back("Set tab in current buffer", [this, entry_tab_char_it](){
      entry_tab_char_it->activate();
    });
    
    entry_box.show();
  }
}

void Window::goto_line_entry() {
  entry_box.clear();
  if(notebook.get_current_page()!=-1) {
    entry_box.entries.emplace_back("", [this](const std::string& content){
      if(notebook.get_current_page()!=-1) {
        auto buffer=notebook.get_current_view()->get_buffer();
        try {
          auto line = stoi(content);
          if(line>0 && line<=buffer->get_line_count()) {
            line--;
            buffer->place_cursor(buffer->get_iter_at_line(line));
            while(gtk_events_pending())
              gtk_main_iteration();
            if(notebook.get_current_page()!=-1)
              notebook.get_current_view()->scroll_to(buffer->get_insert(), 0.0, 1.0, 0.5);
          }
        }
        catch(const std::exception &e) {}  
        entry_box.hide();
      }
    });
    auto entry_it=entry_box.entries.begin();
    entry_it->set_placeholder_text("Line number");
    entry_box.buttons.emplace_back("Go to line", [this, entry_it](){
      entry_it->activate();
    });
    entry_box.show();
  }
}

void Window::rename_token_entry() {
  entry_box.clear();
  if(notebook.get_current_page()!=-1) {
    if(notebook.get_current_view()->get_token) {
      auto token=std::make_shared<Source::Token>(notebook.get_current_view()->get_token());
      if(token) {
        entry_box.labels.emplace_back();
        auto label_it=entry_box.labels.begin();
        label_it->update=[label_it](int state, const std::string& message){
          label_it->set_text("Warning: only opened and parsed tabs will have its content renamed, and modified files will be saved");
        };
        label_it->update(0, "");
        entry_box.entries.emplace_back(token->spelling, [this, token](const std::string& content){
          if(notebook.get_current_page()!=-1 && content!=token->spelling) {
            for(int c=0;c<notebook.size();c++) {
              auto view=notebook.get_view(c);
              if(view->rename_similar_tokens) {
                auto number=view->rename_similar_tokens(*token, content);
                if(number>0) {
                  Singleton::terminal()->print("Replaced "+std::to_string(number)+" occurrences in file "+view->file_path.string()+"\n");
                  notebook.save(c);
                }
              }
            }
            entry_box.hide();
          }
        });
        auto entry_it=entry_box.entries.begin();
        entry_it->set_placeholder_text("New name");
        entry_box.buttons.emplace_back("Rename", [this, entry_it](){
          entry_it->activate();
        });
        entry_box.show();
      }
    }
  }
}
