// SPDX-License-Identifier: GPL-3.0-or-later
// Exact native sources used to prepare the separately downloaded CSO aliases.
// Geometry/material edits at a stock path must use mesh-only adaptation.
#pragma once
#include <string_view>
namespace csol::client {
struct StockHandSource { std::string_view name, sha256; };
inline constexpr StockHandSource stockHandSources[]{
    {"models/v_ak47.mdl","6eb08a5e76210712ad787949e244fd57346b1836a7462f3a436976126e270fa3"},
    {"models/v_aug.mdl","8578a1e5c006377f101efd97d55d7dcbff86a3042e87be39b3d37468bd766e23"},
    {"models/v_awp.mdl","8f27919e666c94af49e802dcdc8b796f0cdf89dcb8e0900371ceffcf44cd9349"},
    {"models/v_c4.mdl","149f4303dcfe7451beefe7517fe7d737b9d8ac9c7a4d04a4ac92fd992426428e"},
    {"models/v_deagle.mdl","5817158952d306c0fbee4d3ef12026078922d1cd4120363e102369a6fdeecec1"},
    {"models/v_elite.mdl","b3ea70b76c6a227e2b2f064d03130d3c999c7aeb367508bd87b35737d61b8e09"},
    {"models/v_famas.mdl","a7000c905a6e90e9e31042773b730f5690a809b933f9af1d77f1ef6df15e4c5a"},
    {"models/v_fiveseven.mdl","66611a04dd0c779d703aa3e5a752358959550d72f394bbcef656150a24e63276"},
    {"models/v_flashbang.mdl","194259d9cf680506608628bc55f8d55a8dd29c4a644bbee185c05cb404444ddb"},
    {"models/v_g3sg1.mdl","d2adeb4082b1bcdd5d3224ec802e84d1ea9a88a517abc35d62747155c56f87f7"},
    {"models/v_galil.mdl","90738d783f37f297c868edcb3a3992f6eab4344af224d1ef0ae4b9300f5183c0"},
    {"models/v_glock18.mdl","da789a2e3e892b3ad8fab86e272e5866e4b50e31b353a5ceb10be03daccae90c"},
    {"models/v_hegrenade.mdl","86f6a5ea2f4fa80b4ce86abb62007b69ea3c500ca0531498630bad1230b0ff41"},
    {"models/v_knife.mdl","f9b753b4a60ad903927fad94644bbd5cd2b849526c7c6a000dd18373e1c08f43"},
    {"models/v_m249.mdl","4590dd20b4ca48357af1c179fcf690f3f4d761245d3efcd12492a0c04974c555"},
    {"models/v_m3.mdl","85855d19382afd3820f74a39ec46c9b7d5e90f2969b965a0c4d6636c5a880824"},
    {"models/v_m4a1.mdl","9248c63dd811d85a062c0d4cdae7213f19a500b4bc73d225c78fa1e61e664033"},
    {"models/v_mac10.mdl","a65101e776983eccd64788118fa121e93649e0908a5db1aeaa5ccad3402517b1"},
    {"models/v_mp5.mdl","33ebb8669a4dfbcba6de9903ed2998c652f7b3f96003380a8900dfd52fdf5316"},
    {"models/v_p228.mdl","57f29c3069dee35be509c18e901f94449944a64b72830cf02215012738fba87b"},
    {"models/v_p90.mdl","6920ecbd5c6d67bd6afdb0ba5ee73fead1429ef9db0adfa8c1b5a24b013e7661"},
    {"models/v_scout.mdl","88d45c5a7ffa5e88443ffb23da350d52db4375049fb8f2946b8bfa48b0cc178b"},
    {"models/v_sg550.mdl","4171144ce78caaf006d412059baf1c1e8217f64a4102e007d98812b651e18ed8"},
    {"models/v_sg552.mdl","79f9d875bb1db81acb5841e8c69674994ca83aab99bcd66aad13b021e346fa74"},
    {"models/v_smokegrenade.mdl","1b863bc5dfe9fcbde92c6f22ac80e09d655b58b5348872d7ac78826debd8452a"},
    {"models/v_tmp.mdl","e654bd1c4dc504a9be348fea6a318f5cb67ad40cab2ffd41395e17300e0aee6a"},
    {"models/v_ump45.mdl","e20bd60f6068228c4aab1c2769ca81ae989b41d7228e3b5299e96560df5fec4c"},
    {"models/v_usp.mdl","401b52af40a1026f5e5241911af4429da6f84291f755ba2e6013cc4246fd2bf0"},
    {"models/v_xm1014.mdl","512fff0de8530bb26847660e429715b2946d55ae719183626252a4950c9cb609"},
};
}
