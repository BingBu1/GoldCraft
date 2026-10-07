#pragma once

namespace goldcraft {
// GoldSrc HLSDK engine/keydefs.h symbols to GLFW's stable key tokens. Mouse
// buttons and wheels use separate event kinds rather than keyboard key codes.
inline int glfw_key(int key){
    if(key>='a'&&key<='z')return key-'a'+'A';
    if(key>=32&&key<127)return key;
    if(key>=135&&key<=146)return 290+key-135;
    switch(key){
        case 9:return 258;case 13:return 257;case 27:return 256;case 127:return 259;
        case 128:return 265;case 129:return 264;case 130:return 263;case 131:return 262;
        case 132:return 342;case 133:return 341;case 134:return 340;
        case 147:return 260;case 148:return 261;case 149:return 267;case 150:return 266;
        case 151:return 268;case 152:return 269;
        case 160:return 327;case 161:return 328;case 162:return 329;case 163:return 324;
        case 164:return 325;case 165:return 326;case 166:return 321;case 167:return 322;
        case 168:return 323;case 169:return 335;case 170:return 320;case 171:return 330;
        case 172:return 331;case 173:return 333;case 174:return 334;case 175:return 280;
        case 255:return 284;default:return -1;
    }
}
}
