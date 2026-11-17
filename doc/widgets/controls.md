# Controls

Controls are the small widgets of a form: what the user clicks, ticks, drags, types or picks,
and what shows a value back.
Each one is a field whose type picks the widget.

| Widget | For | Field type |
|---|---|---|
| [Button](controls/button.md) | an action, or a toggle that stays pressed | `ui::Button` |
| [Checkbox](controls/checkbox.md) | on or off | `bool`, `ui::Checkbox` |
| [Slider](controls/slider.md) | a number in a range, dragged | `ui::Slider<T>` |
| [Spin box](controls/spin-box.md) | a number, typed or stepped | `ui::SpinBox<T>` |
| [Text field](controls/text-field.md) | one line of text, or a password | `ui::TextField<>`, `ui::Password<>` |
| [Text area](controls/text-area.md) | a few lines of text | `ui::TextArea<>` |
| [Combo box](controls/combo-box.md) | free text, with suggestions | `ui::ComboBox` |
| [Choices](controls/choices.md) | one option among several | an enum, `ui::Radio<E>`, `ui::RadioGroup`, `ui::Choice` |
| [Color](controls/color.md) | a color | `core::Color` |
| [Date and time](controls/date-and-time.md) | a day, a time of day | `std::chrono::year_month_day`, `ui::TimeOfDay` |
| [Label](controls/label.md) | read-only text | `std::string`, `ui::Label<>` |
| [Link](controls/link.md) | text that opens a URL | `ui::Link` |
| [Progress bar](controls/progress-bar.md) | how far along work is | `ui::ProgressBar` |
| [Picture](controls/picture.md) | an image | `ui::Picture` |

What every control shares, from tooltips to greying out, is in
[What every widget does](common.md).

```{toctree}
:maxdepth: 1
:hidden:

controls/button
controls/checkbox
controls/slider
controls/spin-box
controls/text-field
controls/text-area
controls/combo-box
controls/choices
controls/color
controls/date-and-time
controls/label
controls/link
controls/progress-bar
controls/picture
```
