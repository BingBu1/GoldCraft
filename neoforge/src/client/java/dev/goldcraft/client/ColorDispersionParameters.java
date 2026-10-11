package dev.goldcraft.client;

/** Effective uniforms of the pinned NightFall 3.4.0 ColorDispersion screen handler. */
final class ColorDispersionParameters {
    private ColorDispersionParameters() {}

    static void write(float[] result,int age,int lifetime,boolean post,float centerX,float centerY) {
        if(age<0||lifetime<=0)throw new IllegalArgumentException("Invalid color-dispersion lifetime");
        // The installed handler uses integer age, not SE_Pipeline.partialTicks.
        float fade=(float)Math.sqrt(Math.max(0f,1f-1f/lifetime*age));
        result[0]=1f+.15f*fade;result[1]=1f+.075f*fade;result[2]=1;
        result[3]=centerX;result[4]=centerY;
        if(!post){result[5]=result[6]=.5f;result[7]=1.2f;}
        else switch(age/8) {
            case 0 -> {result[5]=result[6]=.7f;result[7]=1.1f;}
            case 1 -> {result[5]=result[6]=.5f;result[7]=1.2f;}
            case 2 -> {result[5]=result[6]=.8f;result[7]=1.2f;}
            default -> {result[5]=result[6]=result[7]=0;}
        }
    }
}
