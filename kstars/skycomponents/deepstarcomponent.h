/***************************************************************************
                   deepstarcomponent.h  -  K Desktop Planetarium
                             -------------------
    begin                : Fri 1st Aug 2008
    copyright            : (C) 2008 Akarsh Simha, Thomas Kabelmann
    email                : akarshsimha@gmail.com, thomas.kabelmann@gmx.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef DEEPSTARCOMPONENT_H
#define DEEPSTARCOMPONENT_H

/**
 *@class DeepStarComponent
 *Stores and manages unnamed stars, most of which are dynamically loaded into memory.
 *
 *@author Akarsh Simha
 *@note Much of the code here is copied from class StarComponent authored by Thomas Kabelmann
 *@version 0.1
 */

#include "listcomponent.h"
#include "kstarsdatetime.h"
#include "ksnumbers.h"
#include "starblock.h"
#include "typedef.h"
#include "highpmstarlist.h"
#include "skyobjects/starobject.h"
#include "binfilehelper.h"
#include "starblockfactory.h"
#include "skyobjects/deepstardata.h"
#include "starblocklist.h"

class SkyMesh;
class StarObject;
class SkyLabeler;
class BinFileHelper;
class StarBlockFactory;
class StarBlockList;

class DeepStarComponent: public ListComponent
{
public:

    DeepStarComponent( SkyComposite *parent, QString fileName, float trigMag, bool staticstars=false );

    virtual ~DeepStarComponent();

    //This function is empty; we need that so that the JiT update 
    //is the only one beiong used.
    void update( KSNumbers *num );

    bool selected();

    void draw( SkyPainter *skyp );

    bool loadStaticStars();

    bool openDataFile();

    inline bool hasStaticStars() { return staticStars; }

    float faintMagnitude() const { return m_FaintMagnitude; }

    SkyObject* findByHDIndex( int HDnum );

    SkyObject* objectNearest(SkyPoint *p, double &maxrad );

    inline bool fileOpen() { return fileOpened; }

    inline BinFileHelper *getStarReader() { return &starReader; }

    bool verifySBLIntegrity();

    // TODO: Find the right place for this method
    static void byteSwap( deepStarData *stardata );
    static void byteSwap( starData *stardata );

    static StarBlockFactory m_StarBlockFactory;

private:
    SkyMesh*       m_skyMesh;
    KSNumbers      m_reindexNum;
    int            meshLevel;

    float          m_zoomMagLimit;
    float          triggerMag;       // Magnitude at which this catalog triggers

    float          m_FaintMagnitude; // Limiting magnitude of the catalog currently loaded
    bool           fileOpened;       // Indicates whether the file is opened or not
    float          magLim;           // Current limiting magnitude for visible stars
    unsigned long  visibleStarCount;
    quint16        MSpT;             // Maximum number of stars in any given trixel

    // Time keeping variables
    long unsigned  t_dynamicLoad;
    long unsigned  t_drawUnnamed;
    long unsigned  t_updateCache;

    QVector< StarBlockList *> m_starBlockList;
    QHash<int, StarObject *> m_CatalogNumber;

    bool           staticStars;

    // Stuff required for reading data
    deepStarData  deepstardata;
    starData      stardata;
    BinFileHelper starReader;
    QString       dataFileName;

};

#endif