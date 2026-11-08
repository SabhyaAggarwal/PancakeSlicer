#include "WebHosting.hpp"

#include "slic3r/GUI/GUI.hpp"

#include <libslic3r/Utils.hpp>

#include <boost/filesystem.hpp>

#include <wx/string.h>
#include <wx/uri.h>

namespace Slic3r { namespace GUI { namespace web_hosting {

namespace {

constexpr char ORCA_BRIDGE_JS[] = R"JS(
(function () {
  if (window.top !== window.self) return;
  if (window.orca) return;
  var handlers = [];
  function send(kind, data) {
    try {
      window.wx.postMessage(JSON.stringify({
        channel: 'orca', kind: kind, data: (data === undefined ? null : data)
      }));
    } catch (e) { /* bridge not ready yet */ }
  }
  window.orca = {
    postMessage: function (d) { send('message', d); },
    submit:      function (d) { send('submit', d); },
    close:       function ()  { send('close'); },
    onMessage:   function (cb) { if (typeof cb === 'function') handlers.push(cb); }
  };
  window.__orcaDispatch = function (payload) {
    var data = payload ? payload.data : null;
    for (var i = 0; i < handlers.length; i++) {
      try { handlers[i](data); } catch (e) {}
    }
  };
})();
)JS";

}

wxString bootstrap_url()
{
    return file_url_from_path(boost::filesystem::path(resources_dir()) / BOOTSTRAP_PAGE);
}

wxString content_base_url()
{
    return file_url_from_path(boost::filesystem::path(resources_dir()) / "web") + "/";
}

bool is_content_url(const wxString& url)
{
    return wxURI::Unescape(url.BeforeFirst('#')) == wxURI::Unescape(content_base_url());
}

const char* orca_bridge_script() { return ORCA_BRIDGE_JS; }

}}}
