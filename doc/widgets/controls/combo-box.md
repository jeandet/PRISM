# Combo box

A combo box is a text field with suggestions.
The user types anything, or picks one of the options as they narrow down.

```cpp
struct Order {
    core::Field<ui::ComboBox> fruit{{.placeholder = "Fruit",
                                     .options = {"Apple", "Apricot", "Banana", "Cherry"}}};

    void view(app::ViewBuilder& vb) { vb.widget(fruit); }
};
```

The [text field page](text-field.md) shows a combo box in its screenshot.

## `ui::ComboBox`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `value` | `std::string` | `""` | the text, typed or picked |
| `placeholder` | `std::string` | `""` | shown greyed while it is empty |
| `max_length` | `std::size_t` | `0` | the most characters it takes; `0`: no limit |
| `options` | `std::vector<std::string>` | none | the suggestions |

The value need not be one of the options: this is an editable combo box, as Qt's
`QComboBox::setEditable(true)`.
When the value must be one of a list, use a [choice](choices.md) instead.

## Using it

- Typing edits the text, as in a [text field](text-field.md), and filters the options,
  ignoring case.
- Down and Up move through the list. Enter, or a click, takes the highlighted option.
- Escape closes the list and keeps the text.
- Every edit and every pick notifies.

## See also

- [Choices](choices.md): a dropdown that takes only its options.
- [Completion](../text/completion.md): suggestions inside a document editor.
