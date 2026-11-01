using System.Drawing.Drawing2D;

namespace GoldCraft.Launcher;

internal static class LauncherTheme
{
    public static readonly Color Background=Color.FromArgb(16,23,32);
    public static readonly Color Surface=Color.FromArgb(23,33,45);
    public static readonly Color Border=Color.FromArgb(46,61,76);
    public static readonly Color Text=Color.FromArgb(235,243,248);
    public static readonly Color Muted=Color.FromArgb(150,169,183);
    public static readonly Color Accent=Color.FromArgb(151,219,226);
    public static readonly Color Error=Color.FromArgb(246,164,150);
    public static Font Font(float size,FontStyle style=FontStyle.Regular)=>new("Microsoft YaHei UI",size,style);
    public static GraphicsPath Round(RectangleF r,float radius)
    {
        float d=radius*2;var p=new GraphicsPath();
        p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.Right-d,r.Y,d,d,270,90);
        p.AddArc(r.Right-d,r.Bottom-d,d,d,0,90);p.AddArc(r.X,r.Bottom-d,d,d,90,90);p.CloseFigure();return p;
    }
}

internal enum ButtonTone { Primary,Outline,Quiet }
internal sealed class LauncherButton : Button
{
    [System.ComponentModel.DefaultValue(ButtonTone.Outline)]
    public ButtonTone Tone {get;set;}=ButtonTone.Outline;
    private bool hover,pressed;
    public LauncherButton()
    {
        SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);
        FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;Cursor=Cursors.Hand;Font=LauncherTheme.Font(10);
        BackColor=LauncherTheme.Background;ForeColor=LauncherTheme.Text;Margin=new(0);
    }
    protected override void OnMouseEnter(EventArgs e){hover=true;Invalidate();base.OnMouseEnter(e);}
    protected override void OnMouseLeave(EventArgs e){hover=pressed=false;Invalidate();base.OnMouseLeave(e);}
    protected override void OnMouseDown(MouseEventArgs e){pressed=true;Invalidate();base.OnMouseDown(e);}
    protected override void OnMouseUp(MouseEventArgs e){pressed=false;Invalidate();base.OnMouseUp(e);}
    protected override void OnPaint(PaintEventArgs e)
    {
        var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;
        Color fill=Tone==ButtonTone.Primary?LauncherTheme.Accent:Tone==ButtonTone.Outline?LauncherTheme.Surface:BackColor;
        if(!Enabled)fill=Color.FromArgb(34,48,61);
        else if(pressed)fill=ControlPaint.Dark(fill,0.08f);
        else if(hover)fill=ControlPaint.Light(fill,0.08f);
        using var shape=LauncherTheme.Round(new RectangleF(1,1,Width-2,Height-2),9);
        using var brush=new SolidBrush(fill);g.FillPath(brush,shape);
        if(Tone==ButtonTone.Outline||Focused){using var pen=new Pen(Focused?LauncherTheme.Accent:LauncherTheme.Border);g.DrawPath(pen,shape);}
        Color ink=!Enabled?LauncherTheme.Muted:Tone==ButtonTone.Primary?LauncherTheme.Background:ForeColor;
        TextRenderer.DrawText(g,Text,Font,ClientRectangle,ink,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
    }
}

internal sealed class IceWorld : Control
{
    public IceWorld(){DoubleBuffered=true;BackColor=LauncherTheme.Background;SetStyle(ControlStyles.ResizeRedraw,true);TabStop=false;}
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;
        float scale=Math.Min(Width/440f,Height/350f);g.TranslateTransform((Width-440*scale)/2,(Height-350*scale)/2);g.ScaleTransform(scale,scale);
        using var grid=new Pen(Color.FromArgb(30,47,61),1);
        for(int i=-4;i<9;i++){g.DrawLine(grid,20+i*46,250-i*5,280+i*46,380-i*5);g.DrawLine(grid,5+i*46,382-i*5,265+i*46,252-i*5);}
        void Block(float x,float y,float w,float depth,float height,Color top,Color left,Color right)
        {
            PointF a=new(x,y-height),b=new(x+w,y+w*.5f-height),c=new(x+w-depth,y+(w+depth)*.5f-height),d=new(x-depth,y+depth*.5f-height);
            using var lb=new SolidBrush(left);using var rb=new SolidBrush(right);using var tb=new SolidBrush(top);
            g.FillPolygon(lb,[d,c,new(c.X,c.Y+height),new(d.X,d.Y+height)]);
            g.FillPolygon(rb,[b,c,new(c.X,c.Y+height),new(b.X,b.Y+height)]);
            g.FillPolygon(tb,[a,b,c,d]);
            using var edge=new Pen(Color.FromArgb(65,160,193,207),.8f);g.DrawPolygon(edge,[a,b,c,d]);
        }
        Color snow=Color.FromArgb(187,217,229),ice=Color.FromArgb(75,126,151),shade=Color.FromArgb(39,79,105);
        Block(210,204,155,155,26,Color.FromArgb(55,83,104),Color.FromArgb(30,51,71),Color.FromArgb(22,39,56));
        Block(204,194,112,92,70,snow,ice,shade);
        Block(204,122,112,92,8,Color.FromArgb(218,235,242),Color.FromArgb(128,178,197),Color.FromArgb(96,143,168));
        // A small voxel warehouse and steps connect the two games visually.
        Block(207,104,84,66,15,Color.FromArgb(134,183,201),Color.FromArgb(74,119,144),Color.FromArgb(51,88,112));
        Block(303,237,32,32,34,snow,ice,shade);
        Block(321,257,32,32,18,snow,ice,shade);
        Block(114,225,30,30,45,snow,ice,shade);
        Block(92,242,30,30,23,snow,ice,shade);
        Block(302,152,25,25,32,LauncherTheme.Accent,Color.FromArgb(79,150,169),Color.FromArgb(47,104,130));
        Block(141,290,22,22,12,LauncherTheme.Accent,ice,shade);
        using var door=new SolidBrush(Color.FromArgb(27,57,79));g.FillPolygon(door,[new PointF(218,175),new(243,187.5f),new(243,224),new(218,211.5f)]);
        using var window=new Pen(Color.FromArgb(171,230,235),3);g.DrawLine(window,161,175,183,186);g.DrawLine(window,258,178,278,168);
        using var snowDot=new SolidBrush(Color.FromArgb(93,139,162));
        foreach(var p in new[]{new PointF(100,101),new(336,94),new(354,206),new(72,174),new(237,45)})g.FillRectangle(snowDot,p.X,p.Y,3,3);
    }
}
