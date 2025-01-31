// OpenSTA, Static Timing Analyzer
// Copyright (c) 2023, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "SlewDegradeDelayCalc.hh"

#include "TimingArc.hh"
#include "Liberty.hh"
#include "Network.hh"
#include "Sdc.hh"
#include "Parasitics.hh"
#include "DcalcAnalysisPt.hh"
#include "LumpedCapDelayCalc.hh"

namespace sta {

// Liberty table model lumped capacitance arc delay calculator.
// Effective capacitance is the pi model total capacitance (C1+C2).
// Wire delays are elmore delays.
// Driver slews are degraded to loads by rise/fall transition_degradation
// tables.
class SlewDegradeDelayCalc : public LumpedCapDelayCalc
{
public:
  SlewDegradeDelayCalc(StaState *sta);
  ArcDelayCalc *copy() override;
  void inputPortDelay(const Pin *port_pin,
                      float in_slew,
                      const RiseFall *rf,
                      const Parasitic *parasitic,
                      const DcalcAnalysisPt *dcalc_ap) override;
  void gateDelay(const TimingArc *arc,
                 const Slew &in_slew,
                 float load_cap,
                 const Parasitic *drvr_parasitic,
                 float related_out_cap,
                 const Pvt *pvt,
                 const DcalcAnalysisPt *dcalc_ap,
                 // Return values.
                 ArcDelay &gate_delay,
                 Slew &drvr_slew) override;
  void loadDelay(const Pin *load_pin,
                 ArcDelay &wire_delay,
                 Slew &load_slew) override;

  using LumpedCapDelayCalc::gateDelay;
  using LumpedCapDelayCalc::reportGateDelay;

private:
  const Pvt *pvt_;
};

ArcDelayCalc *
makeSlewDegradeDelayCalc(StaState *sta)
{
  return new SlewDegradeDelayCalc(sta);
}

SlewDegradeDelayCalc::SlewDegradeDelayCalc(StaState *sta) :
  LumpedCapDelayCalc(sta)
{
}

ArcDelayCalc *
SlewDegradeDelayCalc::copy()
{
  return new SlewDegradeDelayCalc(this);
}

void
SlewDegradeDelayCalc::inputPortDelay(const Pin *port_pin,
                                     float in_slew,
                                     const RiseFall *rf,
                                     const Parasitic *parasitic,
                                     const DcalcAnalysisPt *dcalc_ap)
{
  pvt_ = dcalc_ap->operatingConditions();
  LumpedCapDelayCalc::inputPortDelay(port_pin, in_slew, rf, parasitic, dcalc_ap);
}

void
SlewDegradeDelayCalc::gateDelay(const TimingArc *arc,
                                const Slew &in_slew,
                                float load_cap,
                                const Parasitic *drvr_parasitic,
                                float related_out_cap,
                                const Pvt *pvt,
                                const DcalcAnalysisPt *dcalc_ap,
                                // Return values.
                                ArcDelay &gate_delay,
                                Slew &drvr_slew)
{
  input_port_ = false;
  drvr_parasitic_ = drvr_parasitic;
  drvr_rf_ = arc->toEdge()->asRiseFall();
  drvr_cell_ = arc->from()->libertyCell();
  drvr_library_ = drvr_cell_->libertyLibrary();
  pvt_ = pvt;
  LumpedCapDelayCalc::gateDelay(arc, in_slew, load_cap, drvr_parasitic,
                                related_out_cap, pvt, dcalc_ap,
				gate_delay, drvr_slew);
}

void
SlewDegradeDelayCalc::loadDelay(const Pin *load_pin,
                                ArcDelay &wire_delay,
                                Slew &load_slew)
{
  ArcDelay wire_delay1 = 0.0;
  Slew load_slew1 = drvr_slew_;
  bool elmore_exists = false;
  float elmore = 0.0;
  if (drvr_parasitic_)
    parasitics_->findElmore(drvr_parasitic_, load_pin, elmore, elmore_exists);
  if (elmore_exists) {
    if (drvr_library_ && drvr_library_->wireSlewDegradationTable(drvr_rf_)) {
      wire_delay1 = elmore;
      load_slew1 = drvr_library_->degradeWireSlew(drvr_rf_,
						  delayAsFloat(drvr_slew_),
						  delayAsFloat(wire_delay1));
    }
    dspfWireDelaySlew(load_pin, elmore, wire_delay1, load_slew1);
  }
  thresholdAdjust(load_pin, wire_delay1, load_slew1);
  wire_delay = wire_delay1;
  load_slew = load_slew1 * multi_drvr_slew_factor_;
}

} // namespace
