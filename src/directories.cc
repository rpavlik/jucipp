#include "singletons.h"
#include "directories.h"
#include "logging.h"
#include <algorithm>
#include <unordered_set>

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

Directories::Directories() : stop_update_thread(false) {
  JDEBUG("start");
  add(tree_view);
  set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  tree_store = Gtk::TreeStore::create(column_record);
  tree_view.set_model(tree_store);
  tree_view.append_column("", column_record.name);
  auto renderer=dynamic_cast<Gtk::CellRendererText*>(tree_view.get_column(0)->get_first_cell());
  tree_view.get_column(0)->add_attribute(renderer->property_foreground_rgba(), column_record.color);
  
  tree_store->set_sort_column(column_record.id, Gtk::SortType::SORT_ASCENDING);
  tree_view.set_enable_search(true); //TODO: why does this not work in OS X?
  tree_view.set_search_column(column_record.name);
  
  tree_view.signal_row_activated().connect([this](const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column){
    auto iter = tree_store->get_iter(path);
    if (iter) {
      auto path_str=iter->get_value(column_record.path);
      if(path_str!="") {
        if (boost::filesystem::is_directory(boost::filesystem::path(path_str))) {
          tree_view.row_expanded(path) ? tree_view.collapse_row(path) : tree_view.expand_row(path, false);
        } else {
          if(on_row_activated)
            on_row_activated(path_str);
        }
      }
    }
  });
  
  tree_view.signal_test_expand_row().connect([this](const Gtk::TreeModel::iterator& iter, const Gtk::TreeModel::Path& path){
    if(iter->children().begin()->get_value(column_record.path)=="") {
      update_mutex.lock();
      add_path(iter->get_value(column_record.path), *iter);
      update_mutex.unlock();
    }
    return false;
  });
  tree_view.signal_row_collapsed().connect([this](const Gtk::TreeModel::iterator& iter, const Gtk::TreeModel::Path& path){
    update_mutex.lock();
    last_write_times.erase(iter->get_value(column_record.path));
    update_mutex.unlock();
    auto children=iter->children();
    if(children) {
      while(children) {
        tree_store->erase(children.begin());
      }
      auto child=tree_store->append(iter->children());
      child->set_value(column_record.name, std::string("(empty)"));
      Gdk::RGBA rgba;
      rgba.set_rgba(0.5, 0.5, 0.5);
      child->set_value(column_record.color, rgba);
    }
  });
  
  update_dispatcher.connect([this](){
    update_mutex.lock();
    for(auto &path: update_paths) {
      if(last_write_times.count(path)>0)
        add_path(path, last_write_times.at(path).first);
    }
    update_paths.clear();
    update_mutex.unlock();
  });
  
  update_thread=std::thread([this](){
    while(!stop_update_thread) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      update_mutex.lock();
      if(update_paths.size()==0) {
        for(auto it=last_write_times.begin();it!=last_write_times.end();) {
          try {
            if(boost::filesystem::exists(it->first)) { //Added for older boost versions (no exception thrown)
              if(it->second.second<boost::filesystem::last_write_time(it->first)) {
                update_paths.emplace_back(it->first);
              }
              it++;
            }
            else
              it=last_write_times.erase(it);
          }
          catch(const std::exception &e) {
            it=last_write_times.erase(it);
          }
        }
        if(update_paths.size()>0)
          update_dispatcher();
      }
      update_mutex.unlock();
    }
  });
}

Directories::~Directories() {
  stop_update_thread=true;
  update_thread.join();
}

void Directories::open(const boost::filesystem::path& dir_path) {
 JDEBUG("start");
  if(dir_path=="")
    return;
  
  tree_store->clear();
  update_mutex.lock();
  last_write_times.clear();
  update_paths.clear();
  update_mutex.unlock();
    
  cmake=std::unique_ptr<CMake>(new CMake(dir_path));
  auto project=cmake->get_functions_parameters("project");
  if(project.size()>0 && project[0].second.size()>0)
    tree_view.get_column(0)->set_title(project[0].second[0]);
  else
    tree_view.get_column(0)->set_title("");
  update_mutex.lock();
  add_path(dir_path, Gtk::TreeModel::Row());
  update_mutex.unlock();
    
  current_path=dir_path;
  
 JDEBUG("end");
}

void Directories::update() {
 JDEBUG("start");
  update_mutex.lock();
  for(auto &last_write_time: last_write_times) {
    add_path(last_write_time.first, last_write_time.second.first);
  }
  update_mutex.unlock();
 JDEBUG("end");
}

void Directories::select(const boost::filesystem::path &path) {
 JDEBUG("start");
  if(current_path=="")
    return;
    
  if(path.generic_string().substr(0, current_path.generic_string().size()+1)!=current_path.generic_string()+'/')
    return;
  
  std::list<boost::filesystem::path> paths;
  boost::filesystem::path parent_path;
  if(boost::filesystem::is_directory(path))
    parent_path=path;
  else
    parent_path=path.parent_path();
  paths.emplace_front(parent_path);
  while(parent_path!=current_path) {
    parent_path=parent_path.parent_path();
    paths.emplace_front(parent_path);
  }

  for(auto &a_path: paths) {
    tree_store->foreach_iter([this, &a_path](const Gtk::TreeModel::iterator& iter){
      if(iter->get_value(column_record.path)==a_path.string()) {
        update_mutex.lock();
        add_path(a_path, *iter);
        update_mutex.unlock();
        return true;
      }
      return false;
    });
  }
  
  tree_store->foreach_iter([this, &path](const Gtk::TreeModel::iterator& iter){
    if(iter->get_value(column_record.path)==path.string()) {
      auto tree_path=Gtk::TreePath(iter);
      tree_view.expand_to_path(tree_path);
      tree_view.set_cursor(tree_path);
      return true;
    }
    return false;
  });
 JDEBUG("end");
}

bool Directories::ignored(std::string path) {
  std::transform(path.begin(), path.end(), path.begin(), ::tolower);
  
  for(std::string &i : Singleton::Config::directories()->exceptions) {
    if(i == path)
      return false;
  }
  for(auto &i : Singleton::Config::directories()->ignored) {
    if(path.find(i, 0) != std::string::npos)
      return true;
  }
  
  return false;
}

void Directories::add_path(const boost::filesystem::path& dir_path, const Gtk::TreeModel::Row &parent) {
  last_write_times[dir_path.string()]={parent, boost::filesystem::last_write_time(dir_path)};
  std::unique_ptr<Gtk::TreeNodeChildren> children; //Gtk::TreeNodeChildren is missing default constructor...
  if(parent)
    children=std::unique_ptr<Gtk::TreeNodeChildren>(new Gtk::TreeNodeChildren(parent.children()));
  else
    children=std::unique_ptr<Gtk::TreeNodeChildren>(new Gtk::TreeNodeChildren(tree_store->children()));
  if(*children) {
    if(children->begin()->get_value(column_record.path)=="")
      tree_store->erase(children->begin());
  }
  std::unordered_set<std::string> not_deleted;
  boost::filesystem::directory_iterator end_it;
  for(boost::filesystem::directory_iterator it(dir_path);it!=end_it;it++) {
    auto filename=it->path().filename().string();
    if (!ignored(filename)) {
      bool already_added=false;
      if(*children) {
        for(auto &child: *children) {
          if(child.get_value(column_record.name)==filename) {
            not_deleted.emplace(filename);
            already_added=true;
            break;
          }
        }
      }
      if(!already_added) {
        auto child = tree_store->append(*children);
        not_deleted.emplace(filename);
        child->set_value(column_record.name, filename);
        child->set_value(column_record.path, it->path().string());
        if (boost::filesystem::is_directory(*it)) {
          child->set_value(column_record.id, "a"+filename);
          auto grandchild=tree_store->append(child->children());
          grandchild->set_value(column_record.name, std::string("(empty)"));
          Gdk::RGBA rgba;
          rgba.set_rgba(0.5, 0.5, 0.5);
          grandchild->set_value(column_record.color, rgba);
        }
        else {
          child->set_value(column_record.id, "b"+filename);
          
          auto language=Source::guess_language(it->path().filename());
          if(!language) {
            Gdk::RGBA rgba;
            rgba.set_rgba(0.5, 0.5, 0.5);
            child->set_value(column_record.color, rgba);
          }
        }
      }
    }
  }
  if(*children) {
    for(auto it=children->begin();it!=children->end();) {
      if(not_deleted.count(it->get_value(column_record.name))==0) {
        it=tree_store->erase(it);
      }
      else
        it++;
    }
  }
  if(!*children) {
    auto child=tree_store->append(*children);
    child->set_value(column_record.name, std::string("(empty)"));
    Gdk::RGBA rgba;
    rgba.set_rgba(0.5, 0.5, 0.5);
    child->set_value(column_record.color, rgba);
  }
}
