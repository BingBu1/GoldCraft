// SPDX-License-Identifier: GPL-3.0-or-later
#include "validation.hpp"
#include "../../native/csol/client/resources.cpp"
void LoadFixture(model_t* model,void* bytes,void (*original)(model_t*,void*))
{csol::client::loadStudio=original;csol::client::LoadStudio(model,bytes);}
