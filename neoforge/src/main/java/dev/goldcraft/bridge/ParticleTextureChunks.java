package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Protocol25 bounded, row-aligned replacement of one effect texture. */
public final class ParticleTextureChunks {
    public static final int MAX_DIMENSION=4096,MAX_CHUNK_BYTES=256*1024,HEADER_BYTES=48;
    public static final int MAX_TEXTURE_BYTES=MAX_DIMENSION*MAX_DIMENSION*4;
    public record Header(long epoch,long generation,int life,int key,long revision,int width,int height) {
        public Header {
            if(epoch==0||generation<=0||life==0||key<1||key>64||revision<=0
                ||width<1||height<1||width>MAX_DIMENSION||height>MAX_DIMENSION)
                throw new IllegalArgumentException("Particle texture chunk identity/dimensions");
        }
        public int totalBytes(){return width*height*4;}
    }
    private ParticleTextureChunks() {}

    public static byte[] encode(Header header,int offset,ByteBuffer pixels) {
        int row=header.width()*4,total=header.totalBytes();
        if(pixels.remaining()!=total||offset<0||offset>=total||offset%row!=0)
            throw new IllegalArgumentException("Particle texture chunk offset/storage");
        int size=Math.min(total-offset,MAX_CHUNK_BYTES/row*row);
        var out=ByteBuffer.allocate(HEADER_BYTES+size).order(ByteOrder.LITTLE_ENDIAN);
        out.putLong(header.epoch()).putLong(header.generation()).putInt(header.life()).putInt(header.key());
        out.putLong(header.revision()).putInt(header.width()).putInt(header.height()).putInt(offset).putInt(total);
        out.put(pixels.slice(pixels.position()+offset,size));
        return out.array();
    }
}
