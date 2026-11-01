#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/access_tree.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

#include <string>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using prism::ui::Role;

// The role table, pinned at compile time. Values mirror AccessKit's Role discriminants.
static_assert(prism::ui::access_role_of<prism::Button>() == Role::Button);
static_assert(prism::ui::access_role_of<prism::Checkbox>() == Role::CheckBox);
static_assert(prism::ui::access_role_of<bool>() == Role::CheckBox);
static_assert(prism::ui::access_role_of<prism::Slider<>>() == Role::Slider);
static_assert(prism::ui::access_role_of<prism::TextField<>>() == Role::TextInput);
static_assert(prism::ui::access_role_of<prism::Password<>>() == Role::PasswordInput);
static_assert(prism::ui::access_role_of<prism::TextArea<>>() == Role::MultilineTextInput);
static_assert(prism::ui::access_role_of<prism::Label<>>() == Role::Label);
static_assert(prism::ui::access_role_of<int>() == Role::Label);
static_assert(prism::ui::access_role_of<std::string>() == Role::Label);
static_assert(prism::ui::access_role_of<prism::TabBar<>>() == Role::TabList);
static_assert(static_cast<int>(Role::Button) == 18 && static_cast<int>(Role::Slider) == 113);

TEST_CASE("widgets describe their name, value and state") {
    prism::Field<prism::Button> save{{"Save"}};
    CHECK(prism::ui::access_info(save).name == "Save");

    prism::Field<prism::Checkbox> mute{{.checked = true, .label = "Mute"}};
    auto m = prism::ui::access_info(mute);
    CHECK(m.name == "Mute");
    CHECK(m.checked == std::optional<bool>{true});

    prism::Field<prism::Slider<>> vol{{.value = 0.25, .min = 0.0, .max = 1.0, .step = 0.05}};
    auto v = prism::ui::access_info(vol);
    CHECK(v.numeric_value == std::optional<double>{0.25});
    CHECK(v.min_value == std::optional<double>{0.0});
    CHECK(v.max_value == std::optional<double>{1.0});

    prism::Field<prism::TextField<>> text{{.value = "hello"}};
    CHECK(prism::ui::access_info(text).value == "hello");

    prism::Field<int> count{42};
    CHECK(prism::ui::access_info(count).value == "42");
}

TEST_CASE("a password field never exposes its text") {
    prism::Field<prism::Password<>> pw{{.value = "hunter2"}};
    auto info = prism::ui::access_info(pw);
    CHECK(info.role == Role::PasswordInput);
    CHECK(info.value.empty());
}

namespace {
struct Form {
    prism::Field<prism::TextField<>> user{{.value = "ada"}};
    prism::Field<prism::Checkbox> remember{{.checked = false, .label = "Remember me"}};
    prism::Field<prism::Button> login{{"Log in"}};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.vstack(user, remember, login); }
};
}

TEST_CASE("the accessibility tree lists the widgets, without layout-only containers") {
    Form form;
    prism::WidgetTree tree(form);
    tree.set_focused(tree.leaf_ids()[2]);
    auto root = prism::app::build_access_tree(tree);
    REQUIRE(root.children.size() == 3);   // the vstack itself is not a node
    CHECK(root.children[0].info.role == Role::TextInput);
    CHECK(root.children[0].info.value == "ada");
    CHECK(root.children[1].info.name == "Remember me");
    CHECK(root.children[2].info.role == Role::Button);
    CHECK(root.children[2].focused);
    CHECK_FALSE(root.children[0].focused);
#if __cpp_impl_reflection
    CHECK(root.children[0].info.name == "user");   // the reflected member name, in any build
#endif
}
