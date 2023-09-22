// qxgeditMidiDevice.cpp
//
/****************************************************************************
   Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.

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

#include "qxgeditAbout.h"
#include "qxgeditMidiDevice.h"

#include "qxgeditMidiRpn.h"

#include <QThread>
#include <QApplication>

#ifdef CONFIG_ALSA_MIDI
#include <alsa/asoundlib.h>
#endif

#ifdef CONFIG_RTMIDI
#include <RtMidi.h>
#endif

#include <cstdio>


//----------------------------------------------------------------------------
// qxgeditMidiDevice::Impl -- MIDI Device interface object.

class qxgeditMidiDevice::Impl
{
public:

	// Constructor.
	Impl(qxgeditMidiDevice *pMidiDevice, const QString& sClientName);
	// Destructor.
	~Impl();

#ifdef CONFIG_ALSA_MIDI

	// ALSA client descriptor accessor.
	snd_seq_t *alsaSeq() const;
	int alsaClient() const;
	int alsaPort() const;

	// MIDI event capture method.
	void capture(snd_seq_event_t *pEv);

#endif

#ifdef CONFIG_RTMIDI

	// RtMidi descriptor accessors.
	RtMidiIn  *midiIn() const;
	RtMidiOut *midiOut() const;

	// MIDI event capture method.
	void capture(const QByteArray& midi);

#endif

	// MIDI SysEx sender.
	void sendSysex(const QByteArray& sysex) const;
	void sendSysex(unsigned char *pSysex, unsigned short iSysex) const;

	// MIDI Input(readable) / Output(writable) device list
	QStringList inputs() const
		{ return deviceList(true); }
	QStringList outputs() const
		{ return deviceList(false); }

	// MIDI Input(readable) / Output(writable) connects.
	bool connectInputs(const QStringList& inputs) const
		{ return connectDeviceList(true, inputs); }
	bool connectOutputs(const QStringList& outputs) const
		{ return connectDeviceList(false, outputs); }

protected:

	// MIDI device listing.
	QStringList deviceList(bool bReadable) const;

	// MIDI device connects.
	bool connectDeviceList(bool bReadable, const QStringList& list) const;

private:

	// Instance variables.
	qxgeditMidiDevice *m_pMidiDevice;

#ifdef CONFIG_ALSA_MIDI

	snd_seq_t *m_pAlsaSeq;
	int        m_iAlsaClient;
	int        m_iAlsaPort;

	// Name says it all.
	class InputRpn;
	class InputThread;

	InputThread *m_pInputThread;

#endif

#ifdef CONFIG_RTMIDI

	RtMidiIn  *m_pMidiIn;
	RtMidiOut *m_pMidiOut;

	qxgeditMidiRpn m_xrpn;

#endif
};


#ifdef CONFIG_ALSA_MIDI

//----------------------------------------------------------------------
// class qxgeditMidiDevice::InputRpn -- MIDI RPN/NRPN input parser
//

class qxgeditMidiDevice::Impl::InputRpn : public qxgeditMidiRpn
{
public:

	// Constructor.
	InputRpn() : qxgeditMidiRpn() {}

	// Encoder.
	bool process ( const snd_seq_event_t *ev )
	{
		if (ev->type != SND_SEQ_EVENT_CONTROLLER) {
			qxgeditMidiRpn::flush();
			return false;
		}

		qxgeditMidiRpn::Event event;

		event.time   = ev->time.tick;
		event.port   = ev->dest.port;
		event.status = qxgeditMidiRpn::CC | (ev->data.control.channel & 0x0f);
		event.param  = ev->data.control.param;
		event.value  = ev->data.control.value;

		return qxgeditMidiRpn::process(event);
	}

	// Decoder.
	bool dequeue ( snd_seq_event_t *ev )
	{
		qxgeditMidiRpn::Event event;

		if (!qxgeditMidiRpn::dequeue(event))
			return false;

		snd_seq_ev_clear(ev);
		snd_seq_ev_schedule_tick(ev, 0, 0, event.time);
		snd_seq_ev_set_dest(ev, 0, event.port);
		snd_seq_ev_set_fixed(ev);

		switch (qxgeditMidiRpn::Type(event.status & 0x70)) {
		case qxgeditMidiRpn::CC:	// 0x10
			ev->type = SND_SEQ_EVENT_CONTROLLER;
			break;
		case qxgeditMidiRpn::RPN:	// 0x20
			ev->type = SND_SEQ_EVENT_REGPARAM;
			break;
		case qxgeditMidiRpn::NRPN:	// 0x30
			ev->type = SND_SEQ_EVENT_NONREGPARAM;
			break;
		case qxgeditMidiRpn::CC14:	// 0x40
			ev->type = SND_SEQ_EVENT_CONTROL14;
			break;
		default:
			return false;
		}

		ev->data.control.channel = event.status & 0x0f;
		ev->data.control.param   = event.param;
		ev->data.control.value   = event.value;

		return true;
	}
};

//----------------------------------------------------------------------
// class qxgeditMidiDevice::Impl::InputThread -- MIDI input thread (singleton).
//

class qxgeditMidiDevice::Impl::InputThread : public QThread
{
public:

	// Constructor.
	InputThread(Impl *pImpl)
		: QThread(), m_pImpl(pImpl), m_bRunState(false) {}

	// Run-state accessors.
	void setRunState(bool bRunState)
		{ m_bRunState = bRunState; }
	bool runState() const
		{ return m_bRunState; }

protected:

	// The main thread executive.
	void run()
	{
		snd_seq_t *pAlsaSeq = m_pImpl->alsaSeq();
		if (pAlsaSeq == nullptr)
			return;

		int nfds;
		struct pollfd *pfds;

		nfds = snd_seq_poll_descriptors_count(pAlsaSeq, POLLIN);
		pfds = (struct pollfd *) alloca(nfds * sizeof(struct pollfd));
		snd_seq_poll_descriptors(pAlsaSeq, pfds, nfds, POLLIN);

		InputRpn xrpn;

		m_bRunState = true;

		int iPoll = 0;
		while (m_bRunState && iPoll >= 0) {
			// Wait for events...
			iPoll = poll(pfds, nfds, 200);
			// Timeout?
			if (iPoll == 0)
				xrpn.flush();
			while (iPoll > 0) {
				snd_seq_event_t *pEv = nullptr;
				snd_seq_event_input(pAlsaSeq, &pEv);
				// Process input event - ...
				// - enqueue to input track mapping;
				if (!xrpn.process(pEv))
					m_pImpl->capture(pEv);
			//	snd_seq_free_event(pEv);
				iPoll = snd_seq_event_input_pending(pAlsaSeq, 0);
			}
			// Process pending events...
			while (xrpn.isPending()) {
				snd_seq_event_t ev;
				if (xrpn.dequeue(&ev))
					m_pImpl->capture(&ev);
			}
		}
	}

private:

	// The thread launcher engine.
	qxgeditMidiDevice::Impl *m_pImpl;

	// Whether the thread is logically running.
	bool m_bRunState;
};

#endif	// CONFIG_ALSA_MIDI


//----------------------------------------------------------------------------
// qxgeditMidiDevice::Impl -- MIDI Device interface object.


#ifdef CONFIG_RTMIDI

static
void qxgeditMidiDevice_midiIn_callback (
	double delta_time, std::vector<unsigned char> *message, void *user_data )
{
	qxgeditMidiDevice::Impl *pImpl
		= static_cast<qxgeditMidiDevice::Impl *> (user_data);
	if (pImpl) {
		QByteArray midi;
		const unsigned int nsize = message->size();
		for (unsigned int i = 0; i < nsize; ++i) {
			const unsigned char ch = message->at(i);
			midi.append(ch);
			const int status = (ch & 0xf0);
			if (status == 0xf0)
				continue;
			if (status == 0xf7) {
				pImpl->capture(midi);
				midi.clear();
				continue;
			}
			if (++i >= nsize)
				break;
			midi.append(message->at(i));
			if (status == 0xc0 || status == 0xd0) {
				pImpl->capture(midi);
				midi.clear();
				continue;
			}
			if (++i >= nsize)
				break;
			midi.append(message->at(i));
			pImpl->capture(midi);
			midi.clear();
		}
	}
}

#endif	// CONFIG_RTMIDI


// Constructor.
qxgeditMidiDevice::Impl::Impl (
	qxgeditMidiDevice *pMidiDevice, const QString& sClientName )
{
	m_pMidiDevice = pMidiDevice;

#ifdef CONFIG_ALSA_MIDI

	m_pAlsaSeq    = nullptr;
	m_iAlsaClient = -1;
	m_iAlsaPort   = -1;

	m_pInputThread = nullptr;

	// Open new ALSA sequencer client...
	if (snd_seq_open(&m_pAlsaSeq, "hw", SND_SEQ_OPEN_DUPLEX, 0) >= 0) {
		// Set client identification...
		QString sName = sClientName;
		snd_seq_set_client_name(m_pAlsaSeq, sName.toLatin1().constData());
		m_iAlsaClient = snd_seq_client_id(m_pAlsaSeq);
		// Create duplex port
		sName += " MIDI 1";
		m_iAlsaPort = snd_seq_create_simple_port(m_pAlsaSeq,
			sName.toLatin1().constData(),
			SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
			SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
			SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
		// Create and start our own MIDI input queue thread...
		m_pInputThread = new InputThread(this);
		m_pInputThread->start(QThread::TimeCriticalPriority);
	}

#endif	// CONFIG_ALSA_MIDI

#ifdef CONFIG_RTMIDI

	const std::string clientName = sClientName.toStdString();

	try {
		m_pMidiIn  = new RtMidiIn(RtMidi::UNSPECIFIED, clientName);
	}
	catch (RtMidiError &err) {
		err.printMessage();
		m_pMidiIn = nullptr;
	}

	try {
		m_pMidiOut = new RtMidiOut(RtMidi::UNSPECIFIED, clientName);
	}
	catch (RtMidiError &err) {
		err.printMessage();
		m_pMidiOut = nullptr;
	}

	if (m_pMidiIn) {
		// Don't ignore sysex, but timing and active sensing messages...
		m_pMidiIn->ignoreTypes(false);
		m_pMidiIn->setCallback(qxgeditMidiDevice_midiIn_callback, this);
	}

#endif	// CONFIG_RTMIDI
}


qxgeditMidiDevice::Impl::~Impl (void)
{
	// Reset pseudo-singleton reference.
	m_pMidiDevice = nullptr;

#ifdef CONFIG_ALSA_MIDI

	// Last but not least, delete input thread...
	if (m_pInputThread) {
		// Try to terminate executive thread,
		// but give it a bit of time to cleanup...
		if (m_pInputThread->isRunning()) {
			m_pInputThread->setRunState(false);
		//	m_pInputThread->terminate();
			m_pInputThread->wait();
		}
		delete m_pInputThread;
		m_pInputThread = nullptr;
	}

	if (m_pAlsaSeq) {
		snd_seq_delete_simple_port(m_pAlsaSeq, m_iAlsaPort);
		m_iAlsaPort   = -1;
		snd_seq_close(m_pAlsaSeq);
		m_iAlsaClient = -1;
		m_pAlsaSeq    = nullptr;
	}

#endif	// CONFIG_ALSA_MIDI

#ifdef CONFIG_RTMIDI

	delete m_pMidiOut;
	delete m_pMidiIn;

#endif	// CONFIG_RTMIDI
}


#ifdef CONFIG_ALSA_MIDI

// ALSA sequencer client descriptor accessor.
snd_seq_t *qxgeditMidiDevice::Impl::alsaSeq (void) const
{
	return m_pAlsaSeq;
}

int qxgeditMidiDevice::Impl::alsaClient (void) const
{
	return m_iAlsaClient;
}

int qxgeditMidiDevice::Impl::alsaPort (void) const
{
	return m_iAlsaPort;
}


// MIDI event capture method.
void qxgeditMidiDevice::Impl::capture ( snd_seq_event_t *pEv )
{
	// Must be to ourselves...
	if (pEv->dest.port != m_iAlsaPort)
		return;

#ifdef CONFIG_DEBUG
	// - show event for debug purposes...
	::fprintf(stderr, "MIDI In  0x%02x", pEv->type);
	if (pEv->type == SND_SEQ_EVENT_SYSEX) {
		::fprintf(stderr, " sysex {");
		unsigned char *data = (unsigned char *) pEv->data.ext.ptr;
		for (unsigned int i = 0; i < pEv->data.ext.len; ++i)
			::fprintf(stderr, " %02x", data[i]);
		::fprintf(stderr, " }\n");
	} else {
		for (unsigned int i = 0; i < sizeof(pEv->data.raw8.d); ++i)
			::fprintf(stderr, " %3d", pEv->data.raw8.d[i]);
		::fprintf(stderr, "\n");
	}
#endif

	switch (pEv->type) {
	case SND_SEQ_EVENT_REGPARAM:
		// Post RPN event...
		m_pMidiDevice->emitReceiveRpn(
			pEv->data.control.channel,
			pEv->data.control.param,
			pEv->data.control.value);
		break;
	case SND_SEQ_EVENT_NONREGPARAM:
		// Post NRPN event...
		m_pMidiDevice->emitReceiveNrpn(
			pEv->data.control.channel,
			pEv->data.control.param,
			pEv->data.control.value);
		break;
	case SND_SEQ_EVENT_SYSEX:
		// Post SysEx event...
		m_pMidiDevice->emitReceiveSysex(
			QByteArray(
				(const char *) pEv->data.ext.ptr,
				(int) pEv->data.ext.len));
		// Fall thru...
	default:
		break;
	}
}

#endif	// CONFIG_ALSA_MIDI


#ifdef CONFIG_RTMIDI

// ALSA sequencer client descriptor accessor.
RtMidiIn *qxgeditMidiDevice::Impl::midiIn (void) const
{
	return m_pMidiIn;
}

RtMidiOut *qxgeditMidiDevice::Impl::midiOut (void) const
{
	return m_pMidiOut;
}


// MIDI event capture method.
void qxgeditMidiDevice::Impl::capture ( const QByteArray& midi )
{
	if (midi.size() < 3)
		return;

	const int status = (midi.at(0) & 0xf0);

#ifdef CONFIG_DEBUG
	// - show event for debug purposes...
	::fprintf(stderr, "MIDI In  0x%02x", status);
	if (status == 0xf0) {
		::fprintf(stderr, " sysex {");
		for (unsigned int i = 0; i < midi.size() - 1; ++i)
			::fprintf(stderr, " %02x", midi.at(i));
		::fprintf(stderr, " }\n");
	} else {
		::fprintf(stderr, " %2d", midi.at(0) & 0x0f);
		for (unsigned int i = 1; i < midi.size(); ++i)
			::fprintf(stderr, " %3d", midi.at(i) & 0x7f);
		::fprintf(stderr, "\n");
	}
#endif

	// Post SysEx event...
	if (status == 0xf0) {
		m_pMidiDevice->emitReceiveSysex(midi);
		m_xrpn.flush();
	} else {
		qxgeditMidiRpn::Event event;
		if (status == 0xb0) {
			event.time   = 0;
			event.port   = 0;
			event.status = qxgeditMidiRpn::CC | (midi.at(0) & 0x0f);
			event.param  = midi.at(1) & 0x7f;
			event.value  = midi.at(2) & 0x7f;
			m_xrpn.process(event);
		}
		else
		while (m_xrpn.dequeue(event)) {
			const unsigned char channel = (event.status & 0xf0);
			switch (qxgeditMidiRpn::Type(event.status & 0x70)) {
			case qxgeditMidiRpn::RPN:
				m_pMidiDevice->emitReceiveRpn(channel, event.param, event.value);
				break;
			case qxgeditMidiRpn::NRPN:
				m_pMidiDevice->emitReceiveNrpn(channel, event.param, event.value);
				break;
			case qxgeditMidiRpn::CC:
			case qxgeditMidiRpn::CC14:
			default:
				break;
			}
		}
	}
}

#endif	// CONFIG_RTMIDI


void qxgeditMidiDevice::Impl::sendSysex ( const QByteArray& sysex ) const
{
	sendSysex((unsigned char *) sysex.data(), (unsigned short) sysex.length());
}


void qxgeditMidiDevice::Impl::sendSysex (
	unsigned char *pSysex, unsigned short iSysex ) const
{
#ifdef CONFIG_DEBUG
	fprintf(stderr, "qxgeditMidiDevice::sendSysex(%p, %u)", pSysex, iSysex);
	fprintf(stderr, " sysex {");
	for (unsigned short i = 0; i < iSysex; ++i)
		fprintf(stderr, " %02x", pSysex[i]);
	fprintf(stderr, " }\n");
#endif

#ifdef CONFIG_ALSA_MIDI

	// Don't do anything else if engine
	// has not been activated...
	if (m_pAlsaSeq == nullptr)
		return;

	// Initialize sequencer event...
	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);

	// Addressing...
	snd_seq_ev_set_source(&ev, m_iAlsaPort);
	snd_seq_ev_set_subs(&ev);

	// The event will be direct...
	snd_seq_ev_set_direct(&ev);

	// Just set SYSEX stuff and send it out..
	ev.type = SND_SEQ_EVENT_SYSEX;
	snd_seq_ev_set_sysex(&ev, iSysex, pSysex);
	snd_seq_event_output_direct(m_pAlsaSeq, &ev);

#endif	// CONFIG_ALSA_MIDI

#ifdef CONFIG_RTMIDI

	if (m_pMidiOut && m_pMidiOut->isPortOpen())
		m_pMidiOut->sendMessage(pSysex, iSysex);

#endif	// CONFIG_RTMIDI
}


// MIDI Input(readable) / Output(writable) device list.
static const char *c_pszItemSep = " / ";

QStringList qxgeditMidiDevice::Impl::deviceList ( bool bReadable ) const
{
	QStringList list;

#ifdef CONFIG_ALSA_MIDI

	if (m_pAlsaSeq == nullptr)
		return list;

	unsigned int uiPortFlags;
	if (bReadable)
		uiPortFlags = SND_SEQ_PORT_CAP_READ  | SND_SEQ_PORT_CAP_SUBS_READ;
	else
		uiPortFlags = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;

	snd_seq_client_info_t *pClientInfo;
	snd_seq_port_info_t   *pPortInfo;

	snd_seq_client_info_alloca(&pClientInfo);
	snd_seq_port_info_alloca(&pPortInfo);
	snd_seq_client_info_set_client(pClientInfo, -1);

	while (snd_seq_query_next_client(m_pAlsaSeq, pClientInfo) >= 0) {
		int iAlsaClient = snd_seq_client_info_get_client(pClientInfo);
		if (iAlsaClient > 0 && iAlsaClient != m_iAlsaClient) {
			snd_seq_port_info_set_client(pPortInfo, iAlsaClient);
			snd_seq_port_info_set_port(pPortInfo, -1);
			while (snd_seq_query_next_port(m_pAlsaSeq, pPortInfo) >= 0) {
				unsigned int uiPortCapability
					= snd_seq_port_info_get_capability(pPortInfo);
				if (((uiPortCapability & uiPortFlags) == uiPortFlags) &&
					((uiPortCapability & SND_SEQ_PORT_CAP_NO_EXPORT) == 0)) {
					int iAlsaPort = snd_seq_port_info_get_port(pPortInfo);
					QString sItem = QString::number(iAlsaClient) + ':';
					sItem += snd_seq_client_info_get_name(pClientInfo);
					sItem += c_pszItemSep;
					sItem += QString::number(iAlsaPort) + ':';
					sItem += snd_seq_port_info_get_name(pPortInfo);
					list.append(sItem);
				}
			}
		}
	}

#endif	// CONFIG_ALSA_MIDI

#ifdef CONFIG_RTMIDI

	if (bReadable) {
		const unsigned int nports
			= (m_pMidiIn ? m_pMidiIn->getPortCount() : 0);
		for (unsigned int i = 0; i < nports; ++i) {
			list.append(QString::fromStdString(m_pMidiIn->getPortName(i)));
		}
	} else {
		const unsigned int nports
			= (m_pMidiOut ? m_pMidiOut->getPortCount() : 0);
		for (unsigned int i = 0; i < nports; ++i) {
			list.append(QString::fromStdString(m_pMidiOut->getPortName(i)));
		}
	}

#endif	// CONFIG_RTMIDI

	return list;
}


// MIDI Input(readable) / Output(writable) device connects.
bool qxgeditMidiDevice::Impl::connectDeviceList (
	bool bReadable, const QStringList& list ) const
{
	if (list.isEmpty())
		return false;

	int iConnects = 0;

#ifdef CONFIG_ALSA_MIDI

	if (m_pAlsaSeq == nullptr)
		return false;

	snd_seq_addr_t seq_addr;
	snd_seq_port_subscribe_t *pPortSubs;

	snd_seq_port_subscribe_alloca(&pPortSubs);

	snd_seq_client_info_t *pClientInfo;
	snd_seq_port_info_t   *pPortInfo;

	snd_seq_client_info_alloca(&pClientInfo);
	snd_seq_port_info_alloca(&pPortInfo);

	unsigned int uiPortFlags;
	if (bReadable)
		uiPortFlags = SND_SEQ_PORT_CAP_READ  | SND_SEQ_PORT_CAP_SUBS_READ;
	else
		uiPortFlags = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;

	while (snd_seq_query_next_client(m_pAlsaSeq, pClientInfo) >= 0) {
		int iAlsaClient = snd_seq_client_info_get_client(pClientInfo);
		if (iAlsaClient > 0 && iAlsaClient != m_iAlsaClient) {
			QString sClientName = snd_seq_client_info_get_name(pClientInfo);
			snd_seq_port_info_set_client(pPortInfo, iAlsaClient);
			snd_seq_port_info_set_port(pPortInfo, -1);
			while (snd_seq_query_next_port(m_pAlsaSeq, pPortInfo) >= 0) {
				unsigned int uiPortCapability
					= snd_seq_port_info_get_capability(pPortInfo);
				if (((uiPortCapability & uiPortFlags) == uiPortFlags) &&
					((uiPortCapability & SND_SEQ_PORT_CAP_NO_EXPORT) == 0)) {
					int iAlsaPort = snd_seq_port_info_get_port(pPortInfo);
					QString sPortName = snd_seq_port_info_get_name(pPortInfo);
					QStringListIterator iter(list);
					while (iter.hasNext()) {
						const QString& sItem = iter.next();
						const QString& sClientItem
							= sItem.section(c_pszItemSep, 0, 0);
						const QString& sPortItem
							= sItem.section(c_pszItemSep, 1, 1);
						if (sClientName != sClientItem.section(':', 1, 1))
							continue;
						if (sPortName != sPortItem.section(':', 1, 1))
							continue;
						if (bReadable) {
							seq_addr.client = iAlsaClient;
							seq_addr.port   = iAlsaPort;
							snd_seq_port_subscribe_set_sender(pPortSubs, &seq_addr);
							seq_addr.client = m_iAlsaClient;
							seq_addr.port   = m_iAlsaPort;
							snd_seq_port_subscribe_set_dest(pPortSubs, &seq_addr);
						} else {
							seq_addr.client = m_iAlsaClient;
							seq_addr.port   = m_iAlsaPort;
							snd_seq_port_subscribe_set_sender(pPortSubs, &seq_addr);
							seq_addr.client = iAlsaClient;
							seq_addr.port   = iAlsaPort;
							snd_seq_port_subscribe_set_dest(pPortSubs, &seq_addr);
						}
						if (snd_seq_subscribe_port(m_pAlsaSeq, pPortSubs) == 0)
							iConnects++;
					}
				}
			}
		}
	}

#endif	// CONFIG_ALSA_MIDI

#ifdef CONFIG_RTMIDI

	const std::string portName
		= list.first().toStdString();

	if (bReadable) {
		if (m_pMidiIn)
			m_pMidiIn->closePort();
		const unsigned int nports
			= (m_pMidiIn ? m_pMidiIn->getPortCount() : 0);
		for (unsigned int i = 0; i < nports; ++i) {
			if (m_pMidiIn->getPortName(i) == portName) {
				m_pMidiIn->openPort(i, "in");
				++iConnects;
				break;
			}
		}
	} else {
		if (m_pMidiOut)
			m_pMidiOut->closePort();
		const unsigned int nports
			= (m_pMidiOut ? m_pMidiOut->getPortCount() : 0);
		for (unsigned int i = 0; i < nports; ++i) {
			if (m_pMidiOut->getPortName(i) == portName) {
				m_pMidiOut->openPort(i, "out");
				++iConnects;
				break;
			}
		}
	}

#endif	// CONFIG_RTMIDI

	return (iConnects > 0);
}


//----------------------------------------------------------------------------
// qxgeditMidiDevice -- MIDI Device interface object.

// Pseudo-singleton reference.
qxgeditMidiDevice *qxgeditMidiDevice::g_pMidiDevice = nullptr;

// Constructor.
qxgeditMidiDevice::qxgeditMidiDevice ( const QString& sClientName )
	: QObject(nullptr), m_pImpl(new Impl(this, sClientName))
{
	// Set pseudo-singleton reference.
	g_pMidiDevice = this;
}


qxgeditMidiDevice::~qxgeditMidiDevice (void)
{
	// Reset pseudo-singleton reference.
	g_pMidiDevice = nullptr;

	delete m_pImpl;
}


// Pseudo-singleton reference (static).
qxgeditMidiDevice *qxgeditMidiDevice::getInstance (void)
{
	return g_pMidiDevice;
}


void qxgeditMidiDevice::sendSysex ( const QByteArray& sysex ) const
{
	m_pImpl->sendSysex(sysex);
}


void qxgeditMidiDevice::sendSysex (
	unsigned char *pSysex, unsigned short iSysex ) const
{
	m_pImpl->sendSysex(pSysex, iSysex);
}


// MIDI Input(readable) / Output(writable) device list
QStringList qxgeditMidiDevice::inputs (void) const
{
	return m_pImpl->inputs();
}

QStringList qxgeditMidiDevice::outputs (void) const
{
	return m_pImpl->outputs();
}


// MIDI Input(readable) / Output(writable) connects.
bool qxgeditMidiDevice::connectInputs ( const QStringList& inputs ) const
{
	return m_pImpl->connectInputs(inputs);
}

bool qxgeditMidiDevice::connectOutputs ( const QStringList& outputs ) const
{
	return m_pImpl->connectOutputs(outputs);
}


// end of qxgeditMidiDevice.cpp
