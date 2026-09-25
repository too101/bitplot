// BitPlot.cs
//
// Plot a file as bits: 1 byte = 8 points (MSB is the leftmost point).
// Bytes fill downward, one byte per row of 8 points; when a column reaches
// the bottom, plotting continues at the top row, one byte-column further
// to the right (8 points + 1 empty point gap).
//
// Hover the mouse over a point to see the file address (decimal / hex)
// of the byte that point belongs to. The header always shows the address
// of the top-left visible byte.
//
// Build (works on any Windows with .NET Framework 4.x):
//   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:winexe /out:BitPlot.exe BitPlot.cs
//
// Usage: BitPlot.exe [file]   or drag & drop a file onto the window, or press O to open.
// Keys:  O = open file, + / - = zoom point size, mouse wheel = scroll, Ctrl+wheel = zoom.

using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

class Program
{
    [DllImport("user32.dll")]
    static extern bool SetProcessDPIAware();

    [STAThread]
    static void Main(string[] args)
    {
        SetProcessDPIAware();
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new BitPlotForm(args != null && args.Length > 0 ? args[0] : null));
    }
}

class PlotPanel : Panel
{
    public PlotPanel() { DoubleBuffered = true; }
}

class BitPlotForm : Form
{
    byte[] data;
    string fileName;

    int cell = 12;                       // pixels per point
    const int BitsPerByte = 8;
    const int GapPoints = 1;             // empty points between byte columns
    const int MinCell = 4, MaxCell = 40;

    readonly Font font = new Font("Consolas", 11f);
    readonly SolidBrush setBrush = new SolidBrush(Color.Black);
    readonly SolidBrush unsetBrush = new SolidBrush(Color.Gainsboro);
    readonly SolidBrush byteHiBrush = new SolidBrush(Color.FromArgb(70, Color.Orange));
    readonly Pen pointPen = new Pen(Color.Red, 2f);

    readonly Panel header;
    readonly PlotPanel plot;
    readonly HScrollBar hBar;
    readonly Label zoomHint = new Label
    {
        Text = "[+][-] Zoom   [B] MSB",
        ForeColor = Color.DimGray,
        BackColor = Color.White,
        AutoSize = true,
        Font = new Font("Consolas", 11f)
    };

    long hoverByte = -1;                 // file offset of hovered byte, -1 = none
    int hoverBit = -1;                   // bit index 0..7 within the byte (0 = MSB)
    bool lsbFirst = false;               // false = MSB leftmost (normal), true = mirrored font

    long Rows
    {
        get { return Math.Max(1, plot.Height / cell); }
    }
    long Stride
    {
        get { return (BitsPerByte + GapPoints) * cell; }
    }
    long ColCount
    {
        get
        {
            if (data == null || data.Length == 0) return 0;
            return (data.Length + Rows - 1) / Rows;
        }
    }
    int ScrollX
    {
        get { return hBar.Enabled ? hBar.Value : 0; }
    }

    public BitPlotForm(string path)
    {
        Text = "BitPlot";
        MinimumSize = new Size(260, 180);
        ClientSize = new Size(900, 620);   /* size after un-maximizing; same as the Linux default */
        WindowState = FormWindowState.Maximized;
        KeyPreview = true;

        hBar = new HScrollBar { Dock = DockStyle.Bottom, Enabled = false };
        hBar.ValueChanged += delegate { plot.Invalidate(); header.Invalidate(); };

        header = new Panel { Dock = DockStyle.Top, BackColor = Color.White };
        header.Height = (int)Math.Ceiling(font.GetHeight()) + 12;
        header.Controls.Add(zoomHint);
        header.Resize += delegate { PositionZoomHint(); };
        header.Paint += Header_Paint;

        plot = new PlotPanel { Dock = DockStyle.Fill, BackColor = Color.White };
        plot.Paint += Plot_Paint;
        plot.MouseMove += Plot_MouseMove;
        plot.MouseLeave += delegate { SetHover(-1, -1); };
        plot.MouseWheel += Plot_MouseWheel;
        plot.MouseEnter += delegate { plot.Focus(); };
        plot.Resize += delegate { UpdateScroll(); plot.Invalidate(); };

        Controls.Add(plot);
        Controls.Add(header);
        Controls.Add(hBar);

        AllowDrop = true;
        plot.AllowDrop = true;
        DragEnter += OnDragEnter;
        DragDrop += OnDragDrop;
        plot.DragEnter += OnDragEnter;
        plot.DragDrop += OnDragDrop;

        if (path != null) LoadFile(path);
        UpdateTitle();
    }

    void LoadFile(string path)
    {
        try
        {
            data = File.ReadAllBytes(path);
            fileName = Path.GetFileName(path);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "BitPlot", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        hoverByte = -1;
        hoverBit = -1;
        UpdateTitle();
        UpdateScroll();
        plot.Invalidate();
        header.Invalidate();
    }

    void OpenFile()
    {
        using (OpenFileDialog dlg = new OpenFileDialog())
        {
            dlg.Title = "Open file to plot";
            dlg.CheckFileExists = true;
            if (dlg.ShowDialog(this) == DialogResult.OK) LoadFile(dlg.FileName);
        }
    }

    void UpdateTitle()
    {
        if (data == null) Text = "BitPlot";
        else Text = string.Format("BitPlot - {0} ({1} bytes)", fileName, data.Length);
    }

    void UpdateScroll()
    {
        if (data == null || data.Length == 0) { hBar.Enabled = false; hBar.Value = 0; return; }
        long content = ColCount * Stride;
        int view = Math.Max(1, plot.Width);
        long maxScroll = Math.Max(0, content - view);
        if (maxScroll == 0) { hBar.Enabled = false; hBar.Value = 0; return; }
        hBar.Minimum = 0;
        hBar.LargeChange = Math.Min(view, int.MaxValue);
        hBar.Maximum = (int)Math.Min(int.MaxValue, maxScroll + hBar.LargeChange - 1);
        hBar.SmallChange = Math.Max(1, cell);
        if (hBar.Value > hBar.Maximum - hBar.LargeChange + 1) hBar.Value = hBar.Maximum - hBar.LargeChange + 1;
        hBar.Enabled = true;
    }

    void Zoom(int delta)
    {
        int nv = Math.Max(MinCell, Math.Min(MaxCell, cell + delta));
        if (nv == cell) return;
        cell = nv;
        UpdateScroll();
        plot.Invalidate();
        header.Invalidate();
    }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        Keys k = keyData & Keys.KeyCode;
        switch (k)
        {
            case Keys.O: OpenFile(); return true;
            case Keys.Escape: Close(); return true;
            case Keys.B:
                lsbFirst = !lsbFirst;
                zoomHint.Text = "[+][-] Zoom   [B] " + (lsbFirst ? "LSB" : "MSB");
                PositionZoomHint();
                plot.Invalidate();
                return true;
            case Keys.Oemplus:
            case Keys.Add: Zoom(2); return true;
            case Keys.OemMinus:
            case Keys.Subtract: Zoom(-2); return true;
        }
        return base.ProcessCmdKey(ref msg, keyData);
    }

    void Plot_MouseWheel(object sender, MouseEventArgs e)
    {
        if ((Control.ModifierKeys & Keys.Control) != 0) { Zoom(e.Delta > 0 ? 2 : -2); return; }
        if (!hBar.Enabled) return;
        int maxVal = hBar.Maximum - hBar.LargeChange + 1;
        int nv = Math.Max(0, Math.Min(maxVal, hBar.Value - e.Delta));
        if (nv != hBar.Value) hBar.Value = nv;
    }

    void Plot_MouseMove(object sender, MouseEventArgs e)
    {
        if (data == null || data.Length == 0) { SetHover(-1, -1); return; }
        long rows = Rows, stride = Stride;
        long x = e.X + ScrollX;
        long inCol = x % stride;
        if (inCol >= BitsPerByte * cell) { SetHover(-1, -1); return; }   // in the gap between columns
        long row = e.Y / cell;
        if (row >= rows) { SetHover(-1, -1); return; }
        long col = x / stride;
        long idx = col * rows + row;
        if (idx >= data.Length) { SetHover(-1, -1); return; }
        SetHover(idx, (int)(inCol / cell));
    }

    void SetHover(long byteIdx, int bit)
    {
        if (hoverByte == byteIdx && hoverBit == bit) return;
        hoverByte = byteIdx;
        hoverBit = bit;
        plot.Invalidate();
        header.Invalidate();
    }

    void Plot_Paint(object sender, PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        g.Clear(Color.White);

        if (data == null)
        {
            DrawCentered(g, "Drag & drop a file here, or press O to open.", Brushes.Gray);
            return;
        }
        if (data.Length == 0)
        {
            DrawCentered(g, "File is empty.", Brushes.Gray);
            return;
        }

        g.TranslateTransform(-ScrollX, 0);
        long rows = Rows, stride = Stride;
        long cols = ColCount;
        int firstCol = (int)(ScrollX / stride);
        int lastCol = (int)((ScrollX + plot.Width) / stride) + 1;
        if (lastCol > cols - 1) lastCol = (int)cols - 1;

        int pointW = cell - 1;                       // 1px gap between points
        int unsetSize = Math.Max(1, cell / 4);       // small dot for a 0 bit
        int bitsW = BitsPerByte * cell;

        for (int col = firstCol; col <= lastCol; col++)
        {
            long firstByte = (long)col * rows;
            long lastByte = Math.Min((long)data.Length, firstByte + rows);
            float x0 = col * stride;

            if (hoverByte >= firstByte && hoverByte < lastByte)
            {
                int hr = (int)(hoverByte - firstByte);
                g.FillRectangle(byteHiBrush, x0, hr * cell, bitsW, cell);
            }

            for (long b = firstByte; b < lastByte; b++)
            {
                int y = (int)(b - firstByte) * cell;
                byte v = data[b];
                for (int bit = 0; bit < BitsPerByte; bit++)
                {
                    float x = x0 + bit * cell;
                    int bitIndex = lsbFirst ? bit : 7 - bit;
                    if (((v >> bitIndex) & 1) != 0)
                        g.FillRectangle(setBrush, x, y, pointW, pointW);
                    else
                        g.FillRectangle(unsetBrush, x, y, unsetSize, unsetSize);
                }
            }

            if (hoverByte >= firstByte && hoverByte < lastByte && hoverBit >= 0)
            {
                int hr = (int)(hoverByte - firstByte);
                g.DrawRectangle(pointPen, x0 + hoverBit * cell + 0.5f, hr * cell + 0.5f, cell - 2f, cell - 2f);
            }
        }
    }

    void DrawCentered(Graphics g, string s, Brush br)
    {
        using (StringFormat sf = new StringFormat())
        {
            sf.Alignment = StringAlignment.Center;
            sf.LineAlignment = StringAlignment.Center;
            g.DrawString(s, font, br, new RectangleF(0, 0, plot.Width, plot.Height), sf);
        }
    }

    void Header_Paint(object sender, PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        g.Clear(Color.White);

        if (data != null && data.Length > 0)
        {
            using (StringFormat sf = new StringFormat())
            {
                sf.LineAlignment = StringAlignment.Center;

                long addr = hoverByte >= 0 ? hoverByte : (ScrollX / Stride) * Rows;
                string text = string.Format("Address: {0} ({1:X}H)", addr, addr);
                if (addr >= 0 && addr < data.Length)
                    text += string.Format("   Data: {0:X2}H", data[addr]);
                RectangleF left = new RectangleF(8f, 0f, header.Width * 0.5f, header.Height);
                g.DrawString(text, font, Brushes.Black, left, sf);
            }
        }
        using (Pen p = new Pen(Color.LightGray))
            g.DrawLine(p, 0, header.Height - 1, header.Width, header.Height - 1);
    }

    void PositionZoomHint()
    {
        Size sz = TextRenderer.MeasureText(zoomHint.Text, font);
        zoomHint.SetBounds(header.Width - sz.Width - 8,
                           Math.Max(0, (header.Height - sz.Height) / 2),
                           sz.Width, sz.Height);
    }

    void OnDragEnter(object sender, DragEventArgs e)
    {
        if (e.Data.GetDataPresent(DataFormats.FileDrop)) e.Effect = DragDropEffects.Copy;
    }

    void OnDragDrop(object sender, DragEventArgs e)
    {
        string[] files = e.Data.GetData(DataFormats.FileDrop) as string[];
        if (files != null && files.Length > 0) LoadFile(files[0]);
    }
}
