/**
 ******************************************************************************
 *
 * @file       airspeedplugin.h
 * @author     Edouard Lafargue and David Carlson Copyright (C) 2010.
 * @brief
 * @see        The GNU Public License (GPL) Version 3
 * @defgroup   dialplugin
 * @{
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "airspeedplugin.h"
#include "airspeedgadgetfactory.h"
#include <QDebug>
#include <QtPlugin>
#include <QStringList>
#include <extensionsystem/pluginmanager.h>


AirspeedPlugin::AirspeedPlugin()
{
   // Do nothing
}

AirspeedPlugin::~AirspeedPlugin()
{
   // Do nothing
}

bool AirspeedPlugin::initialize(const QStringList& args, QString *errMsg)
{
   Q_UNUSED(args);
   Q_UNUSED(errMsg);
   mf = new AirspeedGadgetFactory(this);
   addAutoReleasedObject(mf);

   return true;
}

void AirspeedPlugin::extensionsInitialized()
{
   // Do nothing
}

void AirspeedPlugin::shutdown()
{
   // Do nothing
}
Q_EXPORT_PLUGIN(AirspeedPlugin)

