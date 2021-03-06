/** *************************************************************************
                          supernovaeitem.h  -  K Desktop Planetarium
                             -------------------
    begin                : 26/06/2016
    copyright            : (C) 2016 by Artem Fedoskin
    email                : afedoskin3@gmail.com
 ***************************************************************************/
/** *************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef SUPERNOVAEITEM_H_
#define SUPERNOVAEITEM_H_

#include "skyitem.h"

class KSComet;
class SkyObject;
class SupernovaeComponent;

    /**
     * @class SupernovaeItem
     * This class handles supernovae in SkyMapLite
     *
     * @author Artem Fedoskin
     * @version 1.0
     */

class SupernovaeItem : public SkyItem {
public:
    /**
     * @short Constructor
     * @param snovaComp - pointer to SupernovaeComponent that handles data
     * @param rootNode parent RootNode that instantiates this object
     */
    SupernovaeItem(SupernovaeComponent *snovaComp, RootNode *rootNode = 0);

    /**
     * @short Recreate the node tree (delete old nodes and append new ones according to
     * SupernovaeItem::objectList())
     */
    void recreateList();

    /**
     * @short Update positions and visibility of supernovae
     */
    virtual void update() override;

private:
    SupernovaeComponent *m_snovaComp;
};
#endif
