using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;
namespace Myime.Settings
{
    // Vertical module navigation grows by scrolling, independently of page width.
    internal sealed class ModuleHost : Panel
    {
        readonly TreeView navigation = new TreeView { Dock = DockStyle.Left, Width = 165, HideSelection = false, ShowLines = false, ShowPlusMinus = false, ShowRootLines = false, ItemHeight = 34, BorderStyle = BorderStyle.None };
        readonly Panel content = new Panel { Dock = DockStyle.Fill };
        internal readonly List<Panel> TabPages = new List<Panel>();
        internal ModuleHost()
        {
            Dock = DockStyle.Fill; Controls.Add(content); Controls.Add(navigation);
            navigation.FontChanged += delegate { navigation.ItemHeight = System.Math.Max(34, TextRenderer.MeasureText("Ag", navigation.Font).Height + 14); };
            navigation.AfterSelect += delegate(object sender, TreeViewEventArgs e) { SelectPage((int)e.Node.Tag); };
        }
        internal Panel AddPage(string name)
        {
            var page = new Panel { Dock = DockStyle.Fill, Padding = new Padding(8), BackColor = SystemColors.Window, Visible = false };
            content.Controls.Add(page); TabPages.Add(page);
            var node = new TreeNode(name) { Tag = TabPages.Count - 1 }; navigation.Nodes.Add(node);
            if (navigation.SelectedNode == null) navigation.SelectedNode = node; return page;
        }
        void SelectPage(int index) { for (int i = 0; i < TabPages.Count; i++) TabPages[i].Visible = i == index; TabPages[index].BringToFront(); }
        internal int SelectedIndex { get { return navigation.SelectedNode == null ? -1 : (int)navigation.SelectedNode.Tag; } set { navigation.SelectedNode = navigation.Nodes[value]; } }
        internal Panel SelectedTab { get { return SelectedIndex < 0 ? null : TabPages[SelectedIndex]; } }
    }
}
