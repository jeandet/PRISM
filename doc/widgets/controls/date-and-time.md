# Date and time

A date field shows a day and opens a month calendar.
A time field shows a time of day, to the minute.

```cpp
struct Booking {
    core::Field<std::chrono::year_month_day> day{std::chrono::year{2026} / std::chrono::November / 14};
    core::Field<ui::TimeOfDay> start{{9, 30}};

    void view(app::ViewBuilder& vb) { vb.hstack(day, start); }
};
```

The [choices page](choices.md) shows a date and a time field in its screenshot.

## The date: `std::chrono::year_month_day`

The field holds the standard library's date type. There is nothing to wrap.

- A click, Space or Enter opens the calendar.
- In the calendar: Left and Right move the date by a day, Up and Down by a week,
  Page Up and Page Down by a month. A click on a day picks it.
- Enter or Escape closes the calendar, and so does a click outside it.
- Each new day notifies, as the keys move it.

## The time: `ui::TimeOfDay`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `hour` | `int` | `0` | 0 to 23 |
| `minute` | `int` | `0` | 0 to 59 |

- Click the hour or the minutes to pick that part. Left and Right switch parts.
- Up and Down, or the mouse wheel, step the part.
- Digits type it: `0` then `7` gives 07.
- Each change notifies.

## See also

- [Choices](choices.md), [Color](color.md): the other pickers.
