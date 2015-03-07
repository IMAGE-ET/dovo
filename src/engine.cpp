
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/config.h>
#include <wx/log.h>

#include "engine.h"
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <boost/spirit/include/classic_core.hpp>
#include <boost/spirit/include/classic_confix.hpp>
#include <boost/spirit/include/classic_escape_char.hpp>
#include "sqlite3_exec_stmt.h"
#include <boost/thread.hpp>

engine::engine()	
{
	db = NULL;

	if(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL))	// everything in utf-8
	{
		std::ostringstream msg;
		msg << "Can't create database: " << sqlite3_errmsg(db);
		throw new std::exception(msg.str().c_str());				
	}	

	sqlite3_exec(db, "CREATE TABLE images (name TEXT, patid TEXT, birthday TEXT, \
					 studyuid TEXT, modality TEXT, studydesc TEXT, studydate TEXT, \
					 seriesuid TEXT, seriesdesc TEXT, \
					 sopuid TEXT UNIQUE, filename TEXT, sent INTEGER)", NULL, NULL, NULL);

}

engine::~engine()
{
	if(db)
		sqlite3_close(db);
}

void engine::SaveDestinationList()
{
	wxConfig::Get()->SetPath("/");
	wxConfig::Get()->DeleteGroup("Destinations");
	for(unsigned int i = 0; i < destinations.size(); i++)
	{
		
		std::wstringstream stream;
		stream << destinations[i].name << "," 
			<< destinations[i].destinationHost << "," 
			<< destinations[i].destinationPort << "," 
			<< destinations[i].destinationAETitle << "," 
			<< destinations[i].ourAETitle;
		
		wxConfig::Get()->SetPath("/Destinations");
		wxConfig::Get()->Write(boost::lexical_cast<std::wstring>(i + 1), stream.str().c_str());
	}

	wxConfig::Get()->Flush();
}

void engine::LoadDestinationList()
{		
	wxConfig::Get()->SetPath("/Destinations");
	wxString str;
	long dummy;
	// first enum all entries
	bool bCont = wxConfig::Get()->GetFirstEntry(str, dummy);
	while ( bCont ) 
	{
		using namespace boost::spirit::classic;

		wxString data;
		std::vector<std::wstring> items;
		data = wxConfig::Get()->Read(str);
		parse(data.ToStdWstring().c_str(),
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			(*anychar_p)[append(items)]
		, space_p);

		if(items.size() == 5)
			destinations.push_back(DestinationEntry(items[0], items[1], boost::lexical_cast<int>(items[2]), items[3], items[4]));

		bCont = wxConfig::Get()->GetNextEntry(str, dummy);
	}
}


void engine::LoadGlobalDestinationList()
{	
	// turn off error message
	wxLogNull nolog;

	// only valid for windows
#if defined(__WINDOWS__) && wxUSE_CONFIG_NATIVE
	wxRegKey registry;
	registry.SetName(wxRegKey::HKLM, "Software\\Policies\\FrontMotion\\fmdeye\\Destinations");
	registry.Open(wxRegKey::Read);
	
	wxString str;
	long dummy;
	// first enum all entries
	bool bCont = registry.GetFirstValue(str, dummy);
	while ( bCont ) 
	{
		using namespace boost::spirit::classic;

		wxString data;
		std::vector<std::wstring> items;
		registry.QueryValue(str, data);
		parse(data.ToStdWstring().c_str(),
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			((*(anychar_p - L','))[append(items)]) >>
			(L',') >>
			(*anychar_p)[append(items)]
		, space_p);

		if(items.size() == 5)
			globalDestinations.push_back(DestinationEntry(items[0].c_str(), items[1].c_str(), boost::lexical_cast<int>(items[2]), items[3].c_str(), items[4].c_str()));

		bCont = registry.GetNextValue(str, dummy);
	}
#endif
}

void engine::StartScan(wxString path)
{
	sqlite3_exec(db, "DELETE FROM images", NULL, NULL, NULL);

	boost::filesystem::path x = path;
	scanner.Initialize(db, x);
	boost::thread t(DICOMFileScanner::DoScanThread, &scanner);
	t.detach();
}

void engine::StopScan()
{
	scanner.Cancel();
}


void engine::StartSend(wxString PatientName, wxString NewPatientName, wxString NewPatientID, wxString NewBirthDay, int destination)
{
	// find the destination
	wxString destinationHost;
	int destinationPort;
	wxString destinationAETitle, ourAETitle;

	if(destination < globalDestinations.size())
	{
		destinationHost = globalDestinations[destination].destinationHost;
		destinationPort = globalDestinations[destination].destinationPort;
		destinationAETitle = globalDestinations[destination].destinationAETitle;
		ourAETitle = globalDestinations[destination].ourAETitle;
	}
	else
	{
		destination -= globalDestinations.size();
		destinationHost = destinations[destination].destinationHost;
		destinationPort = destinations[destination].destinationPort;
		destinationAETitle = destinations[destination].destinationAETitle;
		ourAETitle = destinations[destination].ourAETitle;
	}

	sender.Initialize(db, PatientName.ToUTF8().data(), NewPatientName.ToUTF8().data(), NewPatientID.ToUTF8().data(), NewBirthDay.ToUTF8().data(),
		destinationHost.ToUTF8().data(), destinationPort, destinationAETitle.ToUTF8().data(), ourAETitle.ToUTF8().data());
	
	boost::thread t(DICOMSender::DoSendThread, &sender);
	t.detach(); 
}

void engine::StopSend()
{
	sender.Cancel();
}

void engine::GetPatients(sqlite3_callback fillname, void *obj)
{	
	std::string selectsql = "SELECT name, patid, birthday FROM images GROUP BY name, patid ORDER BY name";
	sqlite3_stmt *select;
	sqlite3_prepare_v2(db, selectsql.c_str(), selectsql.length(), &select, NULL);		
	sqlite3_exec_stmt(select, fillname, obj, NULL);
	sqlite3_finalize(select);	
}
