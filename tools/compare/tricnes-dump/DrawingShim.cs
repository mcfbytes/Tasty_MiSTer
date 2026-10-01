using System;

namespace System.Drawing
{
    public sealed class Bitmap : IDisposable
    {
        public Bitmap(int width, int height, int stride, Imaging.PixelFormat format, IntPtr scan0)
        {
        }

        public void Dispose()
        {
        }
    }
}

namespace System.Drawing.Imaging
{
    public enum PixelFormat
    {
        Format32bppPArgb = 925707,
    }
}
