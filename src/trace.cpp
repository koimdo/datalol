#include "flat/pnr_utils.h"

Initialize::Notify::Notify()
{
  // Normally, this is a base class for *static* intialization, where "uninitialized" head
  // and ready are in fact zero-initialized in the bss.  However, some tests are
  // instantiating it on the stack, where uninitialized values are undefined garbage.
  // In this case, initialize with zeros.
  char dummy;
  ptrdiff_t diff = (char *)this - &dummy;
  if (labs(diff) < (1<<20)) {   // Are we reasonably close to a stack address?
    head = nullptr;
    ready = false;
  }
}

void Initialize::Notify::done()
{
  ready = true;
  for (Initialize *i = head; i; i = i->next)
    i->init_();
}

Initialize::Initialize(Notify& owner): owner(&owner)
{
  next = owner.head;
  owner.head = this;
}

void Initialize::init() {
  if (owner->ready)
    init_();
}

Trace pnr_trace("PMA_PNR_TRACE");

void Trace::parse(const char *value) {
  // parse comma separated values
  std::size_t pos=0;
  while (value[pos]) {
    std::string key = from_string<std::string>(value, &pos);
    if (!value[pos] || value[pos] == ',') {
      // no associated value with this key
      key_value_pairs.insert({key, "1"});
    } else if (value[pos] == ':') {
      // parse associated value
      pos++;
      std::string subvalue;
      if (value[pos] == '{' || value[pos] == '[') {
        subvalue.push_back(value[pos++]);
        while (value[pos] && value[pos] != '}' && value[pos] != ']') {
          subvalue.push_back(value[pos++]);
        }
        if (value[pos])
          subvalue.push_back(value[pos++]);
      } else {
        //subvalue = from_string<std::string>(value, &pos);
      }
      key_value_pairs.insert({key, subvalue});
    }
    if (value[pos])
      pos++;
  }
  TraceOption::all_enabled = (*this)("ALL");
}

bool TraceOption::all_enabled = false;
void TraceOption::init_(void)
{
  // check if key or any of its prefixes is enabled in pnr_trace
  stage = pnr_trace.register_trace_option(msg_prefix, this);
  enabled = pnr_trace(msg_prefix);
  if (enabled) {
    collect_trace_options(msg_prefix);
  }
  if (!enabled) { // check if any of the prefixes is enabled
    std::stringstream tokenizer(msg_prefix);
    std::string prefix="", subkey;
    while (std::getline(tokenizer, subkey, '.')) {
      prefix += ((prefix.length() == 0) ? "" : ".") + subkey;
      if (pnr_trace(prefix)) {
        enabled = true;
        collect_trace_options(prefix);
        break;
      }
    }
  }

}
TraceOption::TraceOption(const std::string& key)
  : Initialize(pnr_trace)
  , msg_prefix(key)
  , out(std::cerr.rdbuf())    // This is only guaranteed to work if libstdc++ is dynamically linked
  , os(this)
{
  init();
}
