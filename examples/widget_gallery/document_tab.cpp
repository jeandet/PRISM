#include "document_tab.hpp"

#include <prism/text/to_markdown.hpp>
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
    auto from = [this](auto&& sync) {
        if (syncing) return;
        syncing = true;
        sync();
        syncing = false;
    };
    wire.on(source, [this, from](const prism::text::Change&) { from([this] { rendered.assign(render(source.text())); }); });
    wire.on(rendered, [this, from](const prism::text::Change&) {
        from([this] { source.assign(prism::text::Document{prism::text::to_markdown(rendered)}); });
    });
}

void DocumentTab::view(prism::app::ViewBuilder& vb) {
    vb.hstack([&] {
        vb.text_editor(source, {.view = {.family = prism::render::FontRole::Mono}, .tab_inserts = true,
                                .auto_indent = true});
        vb.handle();
        vb.text_editor(rendered, {.rich = true});
    });
}

} // namespace gallery
