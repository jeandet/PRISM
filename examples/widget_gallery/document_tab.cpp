#include "document_tab.hpp"

#ifdef GALLERY_MARKDOWN
#include <prism/text/markdown.hpp>
#endif

namespace gallery {

namespace {
// Without prism-markdown (-Dmarkdown=disabled) the source is shown as it is.
prism::text::Document render(const std::string& source) {
#ifdef GALLERY_MARKDOWN
    return prism::text::markdown(source);
#else
    return prism::text::Document{source};
#endif
}
} // namespace

DocumentTab::DocumentTab() : rendered{render(sample_markdown)} {
    wire.on(source, [this](const prism::text::Change&) { rendered.assign(render(source.text())); });
}

void DocumentTab::view(prism::app::ViewBuilder& vb) {
    vb.hstack([&] {
        vb.text_editor(source, {.view = {.family = prism::render::FontRole::Mono}, .tab_inserts = true,
                                .auto_indent = true});
        vb.handle();
        vb.text_view(rendered);
    });
}

} // namespace gallery
