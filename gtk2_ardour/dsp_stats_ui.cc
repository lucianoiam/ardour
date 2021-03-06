/*
 * Copyright (C) 2018 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "gtkmm2ext/utils.h"

#include "ardour/session.h"
#include "ardour/audioengine.h"
#include "ardour/audio_backend.h"

#include "dsp_stats_ui.h"
#include "timers.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Gtkmm2ext;
using namespace Gtk;

DspStatisticsGUI::DspStatisticsGUI ()
	: buffer_size_label ("", ALIGN_RIGHT, ALIGN_CENTER)
	, reset_button (_("Reset"))
{
	const size_t nlabels = Session::NTT + AudioEngine::NTT + AudioBackend::NTT;
	char buf[64];

	labels = new Label*[nlabels];
	snprintf (buf, sizeof (buf), "%7.2f msec %6.2f%%", 10000.0, 100.0);

	for (size_t n = 0; n < nlabels; ++n) {
		labels[n] = new Label ("", ALIGN_RIGHT, ALIGN_CENTER);
		set_size_request_to_display_given_text (*labels[n], buf, 0, 0);
	}

	int row = 0;

	attach (*manage (new Gtk::Label (_("Buffer size: "), ALIGN_RIGHT, ALIGN_CENTER)), 0, 1, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	attach (buffer_size_label, 1, 2, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	row++;

	attach (*manage (new Gtk::Label (_("Idle: "), ALIGN_RIGHT, ALIGN_CENTER)), 0, 1, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	attach (*labels[AudioEngine::NTT + Session::NTT + AudioBackend::DeviceWait], 1, 2, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	row++;

	attach (*manage (new Gtk::Label (_("DSP: "), ALIGN_RIGHT, ALIGN_CENTER)), 0, 1, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	attach (*labels[AudioEngine::NTT + Session::NTT + AudioBackend::RunLoop], 1, 2, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	row++;

	attach (*manage (new Gtk::Label (_("Engine: "), ALIGN_RIGHT, ALIGN_CENTER)), 0, 1, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	attach (*labels[AudioEngine::ProcessCallback], 1, 2, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	row++;

	attach (*manage (new Gtk::Label (_("Session: "), ALIGN_RIGHT, ALIGN_CENTER)), 0, 1, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	attach (*labels[AudioEngine::NTT + Session::OverallProcess], 1, 2, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	row++;

	row++;
	attach (reset_button, 0, 2, row, row+1, Gtk::FILL, Gtk::SHRINK, 2, 0);
	row++;

	reset_button.signal_clicked().connect (sigc::mem_fun (*this, &DspStatisticsGUI::reset_button_clicked));

	show_all ();
}

void
DspStatisticsGUI::reset_button_clicked ()
{
	ARDOUR::reset_performance_meters (_session);
}

void
DspStatisticsGUI::start_updating ()
{
	update ();
	update_connection = Timers::second_connect (sigc::mem_fun(*this, &DspStatisticsGUI::update));
}

void
DspStatisticsGUI::stop_updating ()
{
	update_connection.disconnect ();
}

void
DspStatisticsGUI::update ()
{
	uint64_t min = 0;
	uint64_t max = 0;
	double   avg = 0.;
	double   dev = 0.;
	char buf[64];
	char const * const not_measured_string = X_("--");

	int bufsize = AudioEngine::instance()->samples_per_cycle ();
	double bufsize_usecs = (bufsize * 1000000.0) / AudioEngine::instance()->sample_rate();
	double bufsize_msecs = (bufsize * 1000.0) / AudioEngine::instance()->sample_rate();
	snprintf (buf, sizeof (buf), "%d samples / %5.2f msecs", bufsize, bufsize_msecs);
	buffer_size_label.set_text (buf);

	if (AudioEngine::instance()->current_backend()->dsp_stats[AudioBackend::DeviceWait].get_stats (min, max, avg, dev)) {

		if (min > 1000.0) {
			double minf = min / 1000.0;
			snprintf (buf, sizeof (buf), "%7.2f msec %5.2f%%", minf, (100.0 * minf) / bufsize_msecs);
		} else {
			snprintf (buf, sizeof (buf), "%" PRId64 " usec %5.2f%%", min, (100.0 * min) / bufsize_usecs);
		}
		labels[AudioEngine::NTT + Session::NTT + AudioBackend::DeviceWait]->set_text (buf);
	} else {
		labels[AudioEngine::NTT + Session::NTT + AudioBackend::DeviceWait]->set_text (not_measured_string);
	}

	if (AudioEngine::instance()->current_backend()->dsp_stats[AudioBackend::RunLoop].get_stats (min, max, avg, dev)) {

		if (max > 1000.0) {
			double maxf = max / 1000.0;
			snprintf (buf, sizeof (buf), "%7.2f msec %5.2f%%", maxf, (100.0 * maxf) / bufsize_msecs);
		} else {
			snprintf (buf, sizeof (buf), "%" PRId64 " usec %5.2f%%", max, (100.0 * max) / bufsize_usecs);
		}
		labels[AudioEngine::NTT + Session::NTT + AudioBackend::RunLoop]->set_text (buf);
	} else {
		labels[AudioEngine::NTT + Session::NTT + AudioBackend::RunLoop]->set_text (not_measured_string);
	}

	AudioEngine::instance()->dsp_stats[AudioEngine::ProcessCallback].get_stats (min, max, avg, dev);

	if (_session) {

		uint64_t smin = 0;
		uint64_t smax = 0;
		double   savg = 0.;
		double   sdev = 0.;

		_session->dsp_stats[AudioEngine::ProcessCallback].get_stats (smin, smax, savg, sdev);

		if (smax > 1000.0) {
			double maxf = smax / 1000.0;
			snprintf (buf, sizeof (buf), "%7.2f msec %5.2f%%", maxf, (100.0 * maxf) / bufsize_msecs);
		} else {
			snprintf (buf, sizeof (buf), "%" PRId64 " usec %5.2f%%", smax, (100.0 * smax) / bufsize_usecs);
		}
		labels[AudioEngine::NTT + Session::OverallProcess]->set_text (buf);

		/* Subtract session time from engine process time to show
		 * engine overhead
		 */

		min -= smin;
		max -= smax;
		avg -= savg;
		dev -= sdev;

		if (max > 1000.0) {
			double maxf = max / 1000.0;
			snprintf (buf, sizeof (buf), "%7.2f msec %5.2f%%", maxf, (100.0 * maxf) / bufsize_msecs);
		} else {
			snprintf (buf, sizeof (buf), "%" PRId64 " usec %5.2f%%", max, (100.0 * max) / bufsize_usecs);
		}
		labels[AudioEngine::ProcessCallback]->set_text (buf);

	} else {

		if (max > 1000.0) {
			double maxf = max / 1000.0;
			snprintf (buf, sizeof (buf), "%7.2f msec %5.2f%%", maxf, (100.0 * maxf) / bufsize_msecs);
		} else {
			snprintf (buf, sizeof (buf), "%" PRId64 " usec %5.2f%%", max, (100.0 * max) / bufsize_usecs);
		}
		labels[AudioEngine::ProcessCallback]->set_text (buf);
		labels[AudioEngine::NTT + Session::OverallProcess]->set_text (_("No session loaded"));
	}
}
