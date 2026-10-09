using System;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal static class GridLayout
    {
        internal static void Apply(DataGridView grid)
        {
            grid.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            grid.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.AllCells;
            grid.DefaultCellStyle.Padding = new Padding(3);
            grid.ShowCellToolTips = true; grid.ScrollBars = ScrollBars.Both;
            Action resize = delegate {
                int rowHeight = TextRenderer.MeasureText("Ag国", grid.Font).Height + 12;
                grid.RowTemplate.Height = rowHeight; grid.RowTemplate.MinimumHeight = rowHeight;
                foreach (DataGridViewRow row in grid.Rows) row.MinimumHeight = rowHeight;
                foreach (DataGridViewColumn column in grid.Columns) column.MinimumWidth = Math.Max(80, TextRenderer.MeasureText(column.HeaderText + "      ", grid.Font).Width + 20);
                grid.AutoResizeColumnHeadersHeight();
            };
            grid.FontChanged += delegate { resize(); }; grid.HandleCreated += delegate { resize(); }; grid.ColumnAdded += delegate { resize(); };
            EventHandler languageChanged = delegate { resize(); }; UiText.Changed += languageChanged;
            grid.Disposed += delegate { UiText.Changed -= languageChanged; };
            grid.CellToolTipTextNeeded += delegate(object sender, DataGridViewCellToolTipTextNeededEventArgs e) {
                if (e.RowIndex >= 0 && e.ColumnIndex >= 0) e.ToolTipText = Convert.ToString(grid.Rows[e.RowIndex].Cells[e.ColumnIndex].FormattedValue);
            };
            resize();
        }
        internal static void Stack(Control parent, Control content, params Control[] headings)
        {
            var frame = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = headings.Length + 1, Margin = Padding.Empty, Padding = Padding.Empty };
            for (int index = 0; index < headings.Length; index++) {
                frame.RowStyles.Add(new RowStyle(SizeType.AutoSize)); headings[index].Dock = DockStyle.Fill; frame.Controls.Add(headings[index], 0, index);
            }
            frame.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); content.Dock = DockStyle.Fill;
            frame.Controls.Add(content, 0, headings.Length); parent.Controls.Add(frame);
        }
    }
}
