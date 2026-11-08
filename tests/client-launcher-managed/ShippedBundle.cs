using System.IO.Compression;
using System.Reflection;
using System.Text;
using GoldCraft.Launcher;

static class ShippedBundle
{
    public static (Bundle bundle, byte[] config, byte[] payload) Read(string exe)
    {
        byte[] bytes=File.ReadAllBytes(exe);
        int at=bytes.AsSpan().IndexOf(Convert.FromHexString("8b1202b96a612038727b930214d7a03213f5b9e6efae3318ee3b2dce24b36aae"));
        if(at<8)throw new InvalidDataException("Single-file marker absent");
        using var input=new MemoryStream(bytes);using var reader=new BinaryReader(input,Encoding.UTF8,true);
        input.Position=BitConverter.ToInt64(bytes,at-8);
        if(reader.ReadUInt32()!=6||reader.ReadUInt32()!=0)throw new InvalidDataException("Unknown single-file header");
        int count=reader.ReadInt32();reader.ReadString();input.Position+=40;
        byte[]? managed=null;
        for(int i=0;i<count;i++){
            long offset=reader.ReadInt64(),size=reader.ReadInt64(),compressed=reader.ReadInt64();reader.ReadByte();string name=reader.ReadString();
            if(name!="冰桑启动器.dll")continue;
            using var contents=new MemoryStream(bytes,checked((int)offset),checked((int)(compressed>0?compressed:size)));
            using var copy=new MemoryStream();
            if(compressed>0){using var inflate=new DeflateStream(contents,CompressionMode.Decompress);inflate.CopyTo(copy);}else contents.CopyTo(copy);
            managed=copy.ToArray();if(managed.LongLength!=size)throw new InvalidDataException("Assembly length mismatch");
        }
        var assembly=Assembly.Load(managed??throw new Exception("Assembly absent"));
        byte[] Resource(string name){using var stream=assembly.GetManifestResourceStream(name)??throw new Exception(name);using var copy=new MemoryStream();stream.CopyTo(copy);return copy.ToArray();}
        byte[] config=Resource("bundle.json"),payload=Resource("payload.zip");
        return (new Bundle(JsonData.Read<BundleConfig>(config),()=>new MemoryStream(payload,false)),config,payload);
    }
}
