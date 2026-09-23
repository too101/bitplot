# BitPlot

**View any file as a bitmap of its bits — 1 byte = 8 pixels.**

A tiny Windows tool for finding bitmap fonts embedded inside programs, firmware and ROM dumps — or for eyeballing any raw binary data. Think of it as a hex dump drawn as pixels: structure and patterns (especially glyphs) jump out visually instead of hiding in a wall of hex numbers.

![BitPlot rendering a raw 8×8 bitmap font file — the letters B I T P L O T emerge from the bytes](docs/screenshot.png)

## Download

Grab the prebuilt `BitPlot.exe` from [**Releases**](https://github.com/too101/bitplot/releases/latest) — it's a single portable file, no installation needed (Windows 8.1/10/11 have everything built in).

## How a file is drawn

- Every byte becomes **8 points, most-significant bit on the left**. A `1` bit is a black square, a `0` bit is a faint dot — like `#` and `.` in a hex dump.
- Bytes fill **downward**, one byte per row. When a column reaches the bottom of the window, plotting continues at the **top of the next byte-column**, leaving a 1-point gap between columns — like text flowing down newspaper columns.
- Hover any point and the header shows the **file address of its byte** in decimal and hex: `Address: 1234 (4D2H)`.

Example — a file containing `00 01 02 03 04` renders as:

```
Address: 0 (0H)
........
.......#
......#.
......##
.....#..
```

## Hunting embedded bitmap fonts

Classic bitmap fonts (8×8 and 8×16 PC/BIOS fonts, game fonts, UI fonts baked into firmware) store **one glyph row per byte**, 8 pixels per byte. Plot such a file and the glyph rows line up with the byte columns — the letters become readable, like the screenshot above, which is a raw 56-byte dump of a hand-made 8×8 font spelling `BITPLOT`.

![Hovering a point shows its file address](docs/screenshot-hover.png)

Typical things to point it at:

- firmware / ROM dumps and flash images
- `.exe` / `.dll` / game data files with baked-in fonts
- video BIOS, embedded UI font blobs, raw `.fnt` files
- anything where you suspect 1-bpp graphics

Tips:

- Zoom with `+` / `-` until the column height is a **multiple of the glyph height** (8 or 16 rows), so every glyph starts at the top of a column and stays aligned.
- Use the hover address readout to note where a font starts, then extract it at that offset.
- Files wider than the window scroll horizontally; the window can be resized freely (rows per column follow the window height).

## Usage

```
BitPlot.exe [file]
```

or drag & drop a file onto the window, or press `O` to open one.

| Key | Action |
| --- | --- |
| `O` | open a file |
| `+` / `-` (or `Ctrl` + mouse wheel) | zoom point size |
| mouse wheel | scroll left / right |

## Build

No dependencies beyond Windows itself — any Windows 7–11 with .NET Framework 4.x (preinstalled) can build it from source with the compiler that already ships with Windows:

```
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:winexe /out:BitPlot.exe BitPlot.cs
```

## Files

| File | Description |
| --- | --- |
| `BitPlot.cs` | the whole program in one file (C# WinForms) |
| `font-demo.bin` | hand-made 8×8 bitmap font spelling `BITPLOT` — open it and see the letters |
| `test.bin` | the `00 01 02 03 04 …` example from above |

---

## ภาษาไทย

**BitPlot** คือเครื่องมือเล็กๆ บน Windows สำหรับเปิดไฟล์อะไรก็ได้มาแสดงเป็นบิตแมป — **1 byte = 8 จุด** โดยไบต์จะเรียงลงมาทีละแถว พอถึงล่างสุดของหน้าต่างจะไหลขึ้นไปเริ่มที่บนสุดของคอลัมน์ถัดไป (เว้นช่องว่าง 1 จุด) เหมือนตัวอักษรไหลลงคอลัมน์หนังสือพิมพ์

จุดประสงค์หลักคือใช้**ตามหาฟอนต์ bitmap ที่ฝังอยู่ในโปรแกรม เฟิร์มแวร์ หรือไฟล์เกม** หรือใช้ดูไฟล์ bitmap font โดยตรง เพราะฟอนต์ยุคคลาสสิก (8×8, 8×16) เก็บแต่ละแถวของ glyph เป็น 1 byte = 8 พิกเซล พอ plot ออกมาตัวอักษรจะอ่านได้ทันทีตามในภาพตัวอย่าง

- ดาวน์โหลดโปรแกรมสำเร็จรูปได้ที่หน้า [Releases](https://github.com/too101/bitplot/releases/latest) (ไฟล์เดียว พกพาสะดวก ไม่ต้องติดตั้งอะไร)
- เอาเมาส์ชี้จุดไหนก็จะบอก **address ของ byte นั้น (ฐาน 10 และฐาน 16)** เช่น `Address: 1234 (4D2H)`
- `O` = เปิดไฟล์ (หรือลากไฟล์มาทิ้งในหน้าต่าง), `+` / `-` = ซูมขนาดจุด, ลูกกลิ้งเมาส์ = เลื่อนซ้ายขวา
- เป็น C# WinForms ไฟล์เดียว ไม่มี dependency — คอมไพล์ด้วย `csc.exe` ที่มีในเครื่อง Windows อยู่แล้ว (ดูคำสั่งในหัวข้อ Build)
