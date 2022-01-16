// XGParam.h
//
/****************************************************************************
   Copyright (C) 2005-2022, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#ifndef __XGParam_h
#define __XGParam_h

#include "XGParamObserver.h"

#include <QHash>
#include <QMap>


// Helper prototypes.
const char *getsnote(unsigned short c);


//-------------------------------------------------------------------------
// Forward declarations (XGParam internals)
//

typedef
struct _XGNormalVoiceGroup XGNormalVoiceGroup;

typedef
struct _XGNormalVoiceItem  XGNormalVoiceItem;

typedef
struct _XGDrumKitItem XGDrumKitItem;

typedef
struct _XGDrumVoiceItem  XGDrumVoiceItem;

typedef
struct _XGEffectParamItem XGEffectParamItem;

typedef
struct _XGEffectItem XGEffectItem;

typedef
struct _XGParamItem XGParamItem;

typedef
struct _XGRpnParamItem XGRpnParamItem;


//-------------------------------------------------------------------------
// class XGInstrument - XG Instrument/Normal Voice Group descriptor.
//

class XGInstrument
{
public:

	// Constructor.
	XGInstrument(unsigned short id);

	// Descriptor acessor.
	const XGNormalVoiceGroup *group() const;

	// Instrument name.
	const char *name() const;

	// Number of items.
	unsigned short size() const;

	// Voice index finder.
	int find_voice(unsigned short bank, unsigned short prog) const;

	// Instrument list size.
	static unsigned short count();

private:

	// Parameter descriptor.
	const XGNormalVoiceGroup *m_group;
};


//-------------------------------------------------------------------------
// class XGNormalVoice - XG Normal Voice descriptor.
//

class XGNormalVoice
{
public:

	// Constructor.
	XGNormalVoice(XGInstrument *instr, unsigned short id);

	// Voice properties accessors.
	unsigned short bank() const;
	unsigned short prog() const;
	const char    *name() const;
	unsigned short elem() const;

private:

	// Parameter descriptor.
	const XGNormalVoiceItem *m_item;
};


//-------------------------------------------------------------------------
// class XGDrumKit - XG Drum Kit descriptor.
//

class XGDrumKit
{
public:

	// Constructor.
	XGDrumKit(unsigned short id);

	// Descriptor acessor.
	const XGDrumKitItem *item() const;

	// Drum Kit property accessors.
	unsigned short   bank() const;
	unsigned short   prog() const;
	const char      *name() const;
	unsigned short   size() const;

	// Voice index finder.
	int find_voice(unsigned short key) const;

	// Drum Kit list size.
	static unsigned short count();

private:

	// Parameter descriptor.
	const XGDrumKitItem *m_item;
};


//-------------------------------------------------------------------------
// class XGDrumVoice - XG Drum Voice descriptor.
//

class XGDrumVoice
{
public:

	// Constructor.
	XGDrumVoice(XGDrumKit *drumkit, unsigned short id);

	// Voice properties accessors.
	unsigned short note() const;
	const char    *name() const;

private:

	// Parameter descriptor.
	const XGDrumVoiceItem *m_key;
};


//-------------------------------------------------------------------------
// class XGParam - XG Generic parameter descriptor.
//

class XGParam
{
public:

	// Constructor.
	XGParam(unsigned short high, unsigned short mid, unsigned short low);

	// Virtual destructor.
	virtual ~XGParam();

	// Address acessors.
	unsigned short high() const;
	unsigned short mid()  const;
	unsigned short low()  const;

	// Number of bytes needed to encode subject.
	unsigned short size() const;

	// Virtual accessors.
	virtual const char *name() const;
	virtual unsigned short min() const;
	virtual unsigned short max() const;
	virtual unsigned short def() const;
	virtual float getv(unsigned short u) const;
	virtual unsigned short getu(float v) const;
	virtual const char *gets(unsigned short u) const;
	virtual const char *unit() const;

	// Decode param value from raw 7bit data.
	void set_data_value(unsigned char *data, unsigned short u) const;
	unsigned short data_value(unsigned char *data) const;

	// Decode param value from raw 2byte, 4bit data (eg. DETUNE).
	void set_data_value2(unsigned char *data, unsigned short u) const;
	unsigned short data_value2(unsigned char *data) const;

	// Value accessors.
	void set_value_update(unsigned short u, XGParamObserver *sender = nullptr);
	void set_value(unsigned short u, XGParamObserver *sender = nullptr);
	unsigned short value() const;

	// Virtual reset (to default).
	virtual void reset(XGParamObserver *sender = nullptr);

	// Busy flag predicate.
	bool busy() const;

	// Observers reset notification.
	void notify_reset(XGParamObserver *sender = nullptr);

	// Observers update notification.
	void notify_update(XGParamObserver *sender = nullptr);

	// Observer list accessors.
	void attach(XGParamObserver *observer);
	void detach(XGParamObserver *observer);

	const QList<XGParamObserver *>& observers() const;

	// Textual (name parsed) representations.
	QString label() const;
	QString text() const;

	// Randomizers (p = percent from value/def).
	void randomize_value(float p = 20.0f);
	void randomize_def(float p = 20.0f);

protected:

	// Value randomizer (p = percent from v).
	void randomize(int u, float p);

	// Parameter descriptor.
	const XGParamItem *m_param;

	// Parameter state.
	unsigned short m_value;

private:

	// Parameter address.
	unsigned short m_high;
	unsigned short m_mid;
	unsigned short m_low;

	// Parameter subject/observer stuff;
	bool m_busy;

	QList<XGParamObserver *> m_observers;
};


//-------------------------------------------------------------------------
// class XGEffectParam - XG Effect parameter descriptor.
//
class XGEffectParam : public XGParam
{
public:

	// Constructor.
	XGEffectParam(unsigned short high, unsigned short mid, unsigned short low,
		unsigned short etype);

	// Sub-address accessors.
	unsigned short etype() const;

	// Virtual accessors.
	const char *name() const;
	unsigned short min() const;
	unsigned short max() const;
	unsigned short def() const;
	float getv(unsigned short u) const;
	unsigned short getu(float v) const;
	const char *gets(unsigned short u) const;
	const char *unit() const;

private:

	// Parameter sub-type.
	unsigned short m_etype;	

	// Parameter sub-descriptor.
	const XGEffectParamItem *m_eparam;
};


//-------------------------------------------------------------------------
// class XGDataParam - XG Data parameter descriptor.
//
class XGDataParam : public XGParam
{
public:

	// Constructor.
	XGDataParam(unsigned short high, unsigned short mid, unsigned short low);

	// Destructor.
	~XGDataParam();

	// Data accessors.
	void set_data(
		unsigned char *data, unsigned short len,
		XGParamObserver *sender = nullptr);
	unsigned char *data() const;

	// Virtual reset (to default).
	void reset(XGParamObserver *sender = nullptr);

private:

	// Parameter sub-descriptor.
	unsigned char *m_data;
};


//-------------------------------------------------------------------------
// class XGParamKey - XG Parameter map key.
//
class XGParamKey
{
public:

	// Constructor helper.
	XGParamKey(unsigned short high, unsigned short mid, unsigned short low)
		: m_high(high), m_mid(mid), m_low(low) {}
	XGParamKey(XGParam *param)
		: m_high(param->high()), m_mid(param->mid()), m_low(param->low()) {}

	// Key accessors.
	unsigned short high() const
		{ return m_high; }
	unsigned short mid() const
		{ return m_mid; }
	unsigned short low() const
		{ return m_low; }

	// Map key comparator.
	bool operator< ( const XGParamKey& key ) const
	{
		if (m_high != key.high())
			return (m_high < key.high());
		else
		if (m_mid != key.mid())
			return (m_mid < key.mid());
		else
			return (m_low < key.low());
	}

private:

	// Key members.
	unsigned short m_high;
	unsigned short m_mid;
	unsigned short m_low;
};


//-------------------------------------------------------------------------
// class XGParamSet - XG Parameter map.
//

class XGParamSet : public QHash<unsigned short, XGParam *> {};


//-------------------------------------------------------------------------
// class XGParamMap - XG Parameter mapper.
//

class XGParamMap : public QMap<unsigned short, XGParamSet *>
{
public:

	// Constructor.
	XGParamMap();

	// Destructor.
	~XGParamMap();

	// Append method.
	void add_param(XGParam *param, unsigned short key);

	// Map finders.
	XGParam *find_param(unsigned short id);

	// Key param accessors.
	void set_key_param(XGParam *param);
	XGParam *key_param() const;

	// Key value accessors.
	void set_current_key(unsigned short key);
	unsigned short current_key () const;

	// Param set/factory method.
	XGParamSet *find_paramset(unsigned short id);

	// Local observers notify (key change). 
	void notify_reset();

	// Key/type name stuff.
	typedef QMap<unsigned short, QString> Keys;

	const Keys& keys() const;
	void add_key(unsigned short key, const QString& name);

	// Special element stride settings (USERVOICE, QS300).
	void set_elements(unsigned short elements);
	unsigned short elements() const;

	// Element key value accessors.
	void set_current_element(unsigned short element);
	unsigned short current_element () const;

	// All parameter reset (to default)
	void reset(XGParamObserver *sender = nullptr);

	// All parameter randomizer (p = percent from value/def).
	void randomize_value(int p = 100);
	void randomize_def(int p = 100);

protected:

	// Local observer.
	class Observer : public XGParamObserver
	{
	public:
		// Constructor.
		Observer(XGParamMap *map)
			: XGParamObserver(nullptr), m_map(map) {}

	protected:
		// Observer reset/updater.
		void reset() { m_map->notify_reset(); }
		// Observer updater.
		void update() { reset(); }

	private:
		// Members.
		XGParamMap *m_map;
	};

private:

	// Instance variables.
	XGParam *m_key_param;
	unsigned short m_key;

	// Key param observer.
	Observer *m_observer;

	// Key names registry.
	Keys m_keys;

	// Special element stride settings (USERVOICE, QS300).
	unsigned short m_elements;
	unsigned short m_element;
};


//-------------------------------------------------------------------------
// class XGRpnParamKey - XG (N)RPN Parameter hash key.
//
class XGRpnParamKey
{
public:

	// Constructor helper.
	XGRpnParamKey(unsigned char channel, unsigned short param)
		: m_channel(channel), m_param(param) {}

	// Key accessors.
	unsigned char channel() const
		{ return m_channel; }
	unsigned short param() const
		{ return m_param; }

	// Map key comparator.
	bool operator< ( const XGRpnParamKey& key ) const
	{
		if (m_channel != key.channel())
			return (m_channel < key.channel());
		else
			return (m_channel < key.param());
	}

private:

	// Key members.
	unsigned char  m_channel;
	unsigned short m_param;
};


//-------------------------------------------------------------------------
// class XGRpnParamMap - XG (N)RPN Parameter map.
//

class XGRpnParamMap : public QMap<XGRpnParamKey, XGParam *> {};


//-------------------------------------------------------------------------
// class XGParamMaster - XG Parameter master state database.
//

class XGParamMasterMap : public QMultiMap<XGParamKey, XGParam *>
{
public:

	// Constructor.
	XGParamMasterMap();

	// Destructor.
	~XGParamMasterMap();

	// Pseudo-singleton accessor.
	static XGParamMasterMap *getInstance();

	// Parameter group-maps.
	XGParamMap SYSTEM;
	XGParamMap REVERB;
	XGParamMap CHORUS;
	XGParamMap VARIATION;
	XGParamMap MULTIPART;
	XGParamMap DRUMSETUP;
	XGParamMap USERVOICE;

	// master append method
	void add_param(XGParam *param);

	// Add widget to map.
	void add_param_map(XGParam *param, XGParamMap *map);

	// Master map finders.
	XGParam *find_param(
		unsigned short high,
		unsigned short mid,
		unsigned short low) const;

	XGParam *find_param(
		const XGParamKey& key,
		unsigned short etype = 0) const;

	// Find map from param.
	XGParamMap *find_param_map(XGParam *param) const;

	// NRPN parameter map.
	XGRpnParamMap NRPN;

private:

	// Instance variables.
	QHash<XGParam *, XGParamMap *> m_params_map;

	// Pseudo-singleton reference.
	static XGParamMasterMap *g_pParamMasterMap;
};


#endif	// __XGParam_h

// end of XGParam.h
