#include <iostream>
#include <string>
#include "generic.h"
#include "../../parser.h"
#include "../../moltengamepad.h"
#include "../../eventlists/eventlist.h"

struct context {
  int line_number;
  std::string path;
  simple_messenger* errors;
};

void generic_assignment_line(std::vector<token>& line, generic_driver_info*& info, moltengamepad* mg, context context) {

  auto it = line.begin();
  if (it == line.end()) return;

  std::string field = line.front().value;
  std::string prefix = "";

  it++;

  if (it == line.end()) return;

  if ((*it).type == TK_DOT) {
    it++;
    if (it == line.end()) return;
    prefix = field;
    field = (*it).value;
    it++;

  }


  if ((*it).type != TK_EQUAL) return;

  it++; //Skip past the "="

  if (it == line.end()) return;

  std::string value = (*it).value;
  std::string descr = "(generic)";

  it++;

  if (it != line.end() && (*it).type == TK_COMMA)  {
    it++;
    if (it != line.end() && (*it).type == TK_IDENT)  {
      descr = (*it).value;
    }
  }

  if (field == "name") {
    info->name = value;
    return;
  }

  if (field == "devname") {
    info->devname = value;
    return;
  }

  if (field == "exclusive") {
    if (value == "true") {
      info->grab_ioctl = true;
      return;
    }
    if (value == "false") {
      info->grab_ioctl = false;
      return;
    }
    context.errors->take_message("\""+value + "\" was not recognized as true or false. ("+context.path+":"+std::to_string(context.line_number)+")");
    return;
  }

  if (field == "change_permissions") {
    if (value == "true") {
      info->grab_chmod = true;
      return;
    }
    if (value == "false") {
      info->grab_chmod = false;
      return;
    }
    context.errors->take_message("\""+value + "\" was not recognized as true or false. ("+context.path+":"+std::to_string(context.line_number)+")");
    return;
  }

  if (field == "flatten") {
    if (value == "true") {
      info->flatten = true;
      return;
    }
    if (value == "false") {
      info->flatten = false;
      return;
    }
    context.errors->take_message("\""+value + "\" was not recognized as true or false. ("+context.path+":"+std::to_string(context.line_number)+")");
    return;
  }

  if (field == "split") {
    try {
      int split_count  = std::stoi(value);
      info->split = split_count;
      info->split_types.clear();
      info->split_types.assign(split_count,"gamepad");
    } catch (...) {
      context.errors->take_message("split value invalid ("+context.path+":"+std::to_string(context.line_number)+")");
    }
    return;
  }

  int split_id = 1;
  if (!prefix.empty()) {
    try {
      split_id  = std::stoi(prefix);
      if (split_id <= 0 || split_id > info->split)
        throw -1;
    } catch (...) {
      context.errors->take_message("split value invalid ("+context.path+":"+std::to_string(context.line_number)+")");
    }
  }

  if (field == "device_type") {
    info->split_types.resize(info->split);
    info->split_types[split_id-1] = value;
    return;
  }

  int id = get_key_id(field.c_str());
  if (id != -1) {
    gen_source_event ev;
    ev.name = value;
    ev.id = id;
    ev.descr = descr;
    ev.type = DEV_KEY;
    ev.split_id = split_id;
    info->events.push_back(ev);
    return;
  }

  id = get_axis_id(field.c_str());
  if (id != -1) {
    gen_source_event ev;
    ev.name = value;
    ev.id = id;
    ev.descr = descr;
    ev.type = DEV_AXIS;
    ev.split_id = split_id;
    info->events.push_back(ev);
    return;
  }
  context.errors->take_message(field + " was not recognized as an option or event. ("+context.path+":"+std::to_string(context.line_number)+")");
}

void generic_parse_line(std::vector<token>& line, generic_driver_info*& info, moltengamepad* mg, context context) {

  //if we have a header, check to see if it appears to be starting a new driver, or just adding an extra match.
  
  if (find_token_type(TK_HEADER_OPEN, line)) {
    std::string newhead;
    do_header_line(line, newhead);
    if (!newhead.empty()) {

      if (info->events.size() > 0 && !info->name.empty() && !info->devname.empty()) {
        //This is starting a new driver! Package up the old one and send it off.
        if (!mg->find_manager(info->name.c_str())) {
          mg->managers.push_back(new generic_manager(mg, *info));
        } else {
          context.errors->take_message("redundant driver \""+info->name+"\" ignored");
          delete info;
        }
        info = new generic_driver_info;
      }
      info->matches.push_back({0, 0, std::string(newhead)});
    }
    return;
  }
  if (line.front().value == "alias" && line.size() > 3) {
	  info->aliases.push_back({line[1].value,line[2].value});
	  return;
  }

  if (find_token_type(TK_EQUAL, line)) {
    generic_assignment_line(line, info, mg, context);
    return;
  }

}

int generic_config_loop(moltengamepad* mg, std::istream& in, std::string& path) {
  bool keep_looping = true;
  std::string header = "";
  char* buff = new char [1024];
  bool need_to_free_info = true;
  struct generic_driver_info* info = new generic_driver_info;
  context context;
  context.line_number = 1;
  context.errors = &mg->errors;
  context.path = path;

  while (keep_looping) {
    in.getline(buff, 1024);

    auto tokens = tokenize(std::string(buff));

    if (!tokens.empty() && tokens.front().value == "quit") {
      keep_looping = false;
    }

    generic_parse_line(tokens, info, mg, context);

    context.line_number++;

    if (in.eof()) break;


  }

  if (info->events.size() > 0 && !info->name.empty() && !info->devname.empty()) {
    if (!mg->find_manager(info->name.c_str()))  {
      mg->managers.push_back(new generic_manager(mg, *info));
      need_to_free_info = false;
    } else {
      context.errors->take_message("redundant driver \""+info->name+"\" ignored");
    }
  } else {
    context.errors->take_message("missing name, devname, or events ("+path+")");
  }
  
  if (need_to_free_info) {
    delete info;
  }

  delete[] buff;
  return 0;
}

