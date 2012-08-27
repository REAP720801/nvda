/*
This file is a part of the NVDA project.
URL: http://www.nvda-project.org/
Copyright 2007-2012 NV Access Limited
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2.0, as published by
    the Free Software Foundation.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

#include <windows.h>
#include <set>
#include <string>
#include <sstream>
#include <ia2.h>
#include <common/ia2utils.h>
#include <remote/nvdaHelperRemote.h>
#include <vbufBase/backend.h>
#include <common/log.h>
#include <vbufBase/utils.h>
#include "gecko_ia2.h"

using namespace std;

#define NAVRELATION_NODE_CHILD_OF 0x1005

HWND findRealMozillaWindow(HWND hwnd) {
	if(hwnd==0||!IsWindow(hwnd))
		return (HWND)0;

	wchar_t className[256];
	bool foundWindow=false;
	HWND tempWindow=hwnd;
	do {
		if(GetClassName(tempWindow,className,256)==0)
			return hwnd;
		if(wcscmp(L"MozillaWindowClass",className)!=0)
			foundWindow=true;
		else
			tempWindow=GetAncestor(tempWindow,GA_PARENT);
	} while(tempWindow&&!foundWindow);
	if(GetClassName(tempWindow,className,256)!=0&&wcsstr(className,L"Mozilla")==className)
		hwnd=tempWindow;
	return hwnd;
}

IAccessible2* IAccessible2FromIdentifier(int docHandle, int ID) {
	IAccessible* pacc=NULL;
	IServiceProvider* pserv=NULL;
	IAccessible2* pacc2=NULL;
	VARIANT varChild;
	if(AccessibleObjectFromEvent((HWND)docHandle,OBJID_CLIENT,ID,&pacc,&varChild)!=S_OK) {
		LOG_DEBUG(L"AccessibleObjectFromEvent failed");
		return NULL;
	}
	VariantClear(&varChild);
	if(pacc->QueryInterface(IID_IServiceProvider,(void**)&pserv)!=S_OK) {
		pacc->Release();
		return NULL;
	}  
	pacc->Release();
	pserv->QueryService(IID_IAccessible,IID_IAccessible2,(void**)&pacc2);
	pserv->Release();
	return pacc2;
}

template<typename TableType> inline void fillTableCounts(VBufStorage_controlFieldNode_t* node, IAccessible2* pacc, TableType* paccTable) {
	wostringstream s;
	long count = 0;
	if (paccTable->get_nRows(&count) == S_OK) {
		s << count;
		node->addAttribute(L"table-rowcount", s.str());
		s.str(L"");
	}
	if (paccTable->get_nColumns(&count) == S_OK) {
		s << count;
		node->addAttribute(L"table-columncount", s.str());
	}
}

inline void fillTableCellInfo_IATable(VBufStorage_controlFieldNode_t* node, IAccessibleTable* paccTable, const wstring& cellIndexStr) {
	wostringstream s;
	long cellIndex = _wtoi(cellIndexStr.c_str());
	long row, column, rowExtents, columnExtents;
	boolean isSelected;
	if (paccTable->get_rowColumnExtentsAtIndex(cellIndex, &row, &column, &rowExtents, &columnExtents, &isSelected) == S_OK) {
		s << row + 1;
		node->addAttribute(L"table-rownumber", s.str());
		s.str(L"");
		s << column + 1;
		node->addAttribute(L"table-columnnumber", s.str());
		if (columnExtents > 1) {
			s.str(L"");
			s << columnExtents;
			node->addAttribute(L"table-columnsspanned", s.str());
		}
		if (rowExtents > 1) {
			s.str(L"");
			s << rowExtents;
			node->addAttribute(L"table-rowsspanned", s.str());
		}
	}
}

typedef HRESULT(STDMETHODCALLTYPE IAccessibleTableCell::*IATableCellGetHeaderCellsFunc)(IUnknown***, long*);
inline void fillTableHeaders(VBufStorage_controlFieldNode_t* node, IAccessibleTableCell* paccTableCell, const IATableCellGetHeaderCellsFunc getHeaderCells, const wstring& attribName) {
	wostringstream s;
	IUnknown** headerCells;
	long nHeaderCells;
	IAccessible2* headerCellPacc = NULL;
	int headerCellDocHandle, headerCellID;

	if ((paccTableCell->*getHeaderCells)(&headerCells, &nHeaderCells) == S_OK && headerCells) {
		for (int hci = 0; hci < nHeaderCells; hci++) {
			if (headerCells[hci]->QueryInterface(IID_IAccessible2, (void**)&headerCellPacc) != S_OK) {
				headerCells[hci]->Release();
				continue;
			}
			headerCells[hci]->Release();
			if (headerCellPacc->get_windowHandle((HWND*)&headerCellDocHandle) != S_OK) {
				headerCellPacc->Release();
				continue;
			}
			headerCellDocHandle = (int)findRealMozillaWindow((HWND)headerCellDocHandle);
			if (headerCellPacc->get_uniqueID((long*)&headerCellID) != S_OK) {
				headerCellPacc->Release();
				continue;
			}
			s << headerCellDocHandle << L"," << headerCellID << L";";
			headerCellPacc->Release();
		}
		if (!s.str().empty())
			node->addAttribute(attribName, s.str());
		CoTaskMemFree(headerCells);
	}
}

inline void GeckoVBufBackend_t::fillTableCellInfo_IATable2(VBufStorage_controlFieldNode_t* node, IAccessibleTableCell* paccTableCell) {
	wostringstream s;

	long row, column, rowExtents, columnExtents;
	boolean isSelected;
	if (paccTableCell->get_rowColumnExtents(&row, &column, &rowExtents, &columnExtents, &isSelected) == S_OK) {
		s << row + 1;
		node->addAttribute(L"table-rownumber", s.str());
		s.str(L"");
		s << column + 1;
		node->addAttribute(L"table-columnnumber", s.str());
		if (columnExtents > 1) {
			s.str(L"");
			s << columnExtents;
			node->addAttribute(L"table-columnsspanned", s.str());
		}
		if (rowExtents > 1) {
			s.str(L"");
			s << rowExtents;
			node->addAttribute(L"table-rowsspanned", s.str());
		}
	}

	if (this->shouldDisableTableHeaders)
		return;

	fillTableHeaders(node, paccTableCell, &IAccessibleTableCell::get_columnHeaderCells, L"table-columnheadercells");
	fillTableHeaders(node, paccTableCell, &IAccessibleTableCell::get_rowHeaderCells, L"table-rowheadercells");
}

void GeckoVBufBackend_t::versionSpecificInit(IAccessible2* pacc) {
	// Defaults.
	this->shouldDisableTableHeaders = false;
	this->hasEncodedAccDescription = false;

	IServiceProvider* serv = NULL;
	if (pacc->QueryInterface(IID_IServiceProvider, (void**)&serv) != S_OK)
		return;
	IAccessibleApplication* iaApp = NULL;
	if (serv->QueryService(IID_IAccessibleApplication, IID_IAccessibleApplication, (void**)&iaApp) != S_OK) {
		serv->Release();
		return;
	}
	serv->Release();

	BSTR toolkitName = NULL;
	if (iaApp->get_toolkitName(&toolkitName) != S_OK) {
		iaApp->Release();
		return;
	}
	BSTR toolkitVersion = NULL;
	if (iaApp->get_toolkitVersion(&toolkitVersion) != S_OK) {
		iaApp->Release();
		SysFreeString(toolkitName);
		return;
	}
	iaApp->Release();
	iaApp = NULL;

	if (wcscmp(toolkitName, L"Gecko") == 0) {
		if (wcsncmp(toolkitVersion, L"1.", 2) == 0) {
			if (wcsncmp(toolkitVersion, L"1.9.2.", 6) == 0) {
				// Gecko 1.9.2.x.
				// Retrieve the digits for the final part of the main version number.
				wstring verPart;
				for (wchar_t* c = &toolkitVersion[6]; iswdigit(*c); c++)
					verPart += *c;
				if (_wtoi(verPart.c_str()) <= 10) {
					// Gecko <= 1.9.2.10 will crash if we try to retrieve headers on some table cells, so disable them.
					this->shouldDisableTableHeaders = true;
				}
			}
			// Gecko 1.x uses accDescription to encode position info as well as the description.
			this->hasEncodedAccDescription = true;
		}
	}

	SysFreeString(toolkitName);
	SysFreeString(toolkitVersion);
}

VBufStorage_fieldNode_t* GeckoVBufBackend_t::fillVBuf(IAccessible2* pacc, VBufStorage_buffer_t* buffer, VBufStorage_controlFieldNode_t* parentNode, VBufStorage_fieldNode_t* previousNode, IAccessibleTable* paccTable, IAccessibleTable2* paccTable2, long tableID) {
	nhAssert(buffer); //buffer can't be NULL
	nhAssert(!parentNode||buffer->isNodeInBuffer(parentNode)); //parent node must be in buffer
	nhAssert(!previousNode||buffer->isNodeInBuffer(previousNode)); //Previous node must be in buffer
	VBufStorage_fieldNode_t* tempNode;
	//all IAccessible methods take a variant for childID, get one ready
	VARIANT varChild;
	varChild.vt=VT_I4;
	varChild.lVal=0;
	wostringstream s;

	//get docHandle -- IAccessible2 windowHandle
	int docHandle;
	if(pacc->get_windowHandle((HWND*)&docHandle)!=S_OK) {
		LOG_DEBUG(L"pacc->get_windowHandle failed");
		return NULL;
	}
	docHandle=(int)findRealMozillaWindow((HWND)docHandle);
	if(!docHandle) {
		LOG_DEBUG(L"bad docHandle");
		return NULL;
	}
	//Get ID -- IAccessible2 uniqueID
	int ID;
	if(pacc->get_uniqueID((long*)&ID)!=S_OK) {
		LOG_DEBUG(L"pacc->get_uniqueID failed");
		return NULL;
	}

	//Make sure that we don't already know about this object -- protect from loops
	if(buffer->getControlFieldNodeWithIdentifier(docHandle,ID)!=NULL) {
		LOG_DEBUG(L"a node with this docHandle and ID already exists, returning NULL");
		return NULL;
	}

	//Add this node to the buffer
	parentNode=buffer->addControlFieldNode(parentNode,previousNode,docHandle,ID,TRUE);
	nhAssert(parentNode); //new node must have been created
	previousNode=NULL;

	//Get role -- IAccessible2 role
	long role=0;
	BSTR roleString=NULL;
	if(pacc->role(&role)!=S_OK)
		role=IA2_ROLE_UNKNOWN;
	VARIANT varRole;
	VariantInit(&varRole);
	if(role==0) {
		if(pacc->get_accRole(varChild,&varRole)!=S_OK) {
			LOG_DEBUG(L"accRole failed");
		}
		if(varRole.vt==VT_I4)
			role=varRole.lVal;
		else if(varRole.vt==VT_BSTR)
			roleString=varRole.bstrVal;
	}
	//Add role as an attrib
	if(roleString!=NULL)
		s<<roleString;
	else
		s<<role;
	parentNode->addAttribute(L"IAccessible::role",s.str());
	s.str(L"");
	VariantClear(&varRole);

	//get states -- IAccessible accState
	VARIANT varState;
	VariantInit(&varState);
	if(pacc->get_accState(varChild,&varState)!=S_OK) {
		LOG_DEBUG(L"pacc->get_accState returned "<<res);
		varState.vt=VT_I4;
		varState.lVal=0;
	}
	int states=varState.lVal;
	VariantClear(&varState);
	//Add each state that is on, as an attrib
	for(int i=0;i<32;++i) {
		int state=1<<i;
		if(state&states) {
			s<<L"IAccessible::state_"<<state;
			parentNode->addAttribute(s.str(),L"1");
			s.str(L"");
		}
	}
	//get IA2States -- IAccessible2 states
	AccessibleStates IA2States;
	if(pacc->get_states(&IA2States)!=S_OK) {
		LOG_DEBUG(L"pacc->get_states failed");
		IA2States=0;
	}
	//Add each state that is on, as an attrib
	for(int i=0;i<32;++i) {
		int state=1<<i;
		if(state&IA2States) {
			s<<L"IAccessible2::state_"<<state;
			parentNode->addAttribute(s.str(),L"1");
			s.str(L"");
		}
	}

	//get keyboardShortcut -- IAccessible accKeyboardShortcut;
	BSTR keyboardShortcut;
	if(pacc->get_accKeyboardShortcut(varChild,&keyboardShortcut)==S_OK) {
		parentNode->addAttribute(L"keyboardShortcut",keyboardShortcut);
		//Free keyboardShortcut string memory
		SysFreeString(keyboardShortcut);
	} else
		parentNode->addAttribute(L"keyboardShortcut",L"");

	//get IA2Attributes -- IAccessible2 attributes;
	BSTR IA2Attributes;
	map<wstring,wstring> IA2AttribsMap;
		if(pacc->get_attributes(&IA2Attributes)==S_OK) {
		IA2AttribsToMap(IA2Attributes,IA2AttribsMap);
		SysFreeString(IA2Attributes);
		// Add each IA2 attribute as an attrib.
		for(map<wstring,wstring>::const_iterator it=IA2AttribsMap.begin();it!=IA2AttribsMap.end();++it) {
			s<<L"IAccessible2::attribute_"<<it->first;
			parentNode->addAttribute(s.str(),it->second);
			s.str(L"");
		}
	} else
		LOG_DEBUG(L"pacc->get_attributes failed");
	map<wstring,wstring>::const_iterator IA2AttribsMapIt;

	BSTR defaction=NULL;
	if(pacc->get_accDefaultAction(varChild,&defaction)==S_OK) {
		if(defaction&&SysStringLen(defaction)==0) {
			SysFreeString(defaction);
			defaction=NULL;
		}
	}
	if(defaction)
		parentNode->addAttribute(L"defaultAction",defaction);

	//Check IA2Attributes, and or the role etc to work out if this object is a block element
	bool isBlockElement=TRUE;
	if(IA2States&IA2_STATE_MULTI_LINE) {
		// Multiline nodes should always be block.
		isBlockElement=TRUE;
	} else if((IA2AttribsMapIt=IA2AttribsMap.find(L"display"))!=IA2AttribsMap.end()) {
		// If there is a display attribute, we can rely solely on this to determine whether this is a block element or not.
		isBlockElement=(IA2AttribsMapIt->second!=L"inline"&&IA2AttribsMapIt->second!=L"inline-block");
	} else if((IA2AttribsMapIt=IA2AttribsMap.find(L"formatting"))!=IA2AttribsMap.end()&&IA2AttribsMapIt->second==L"block") {
		isBlockElement=TRUE;
	} else if(role==ROLE_SYSTEM_TABLE||role==ROLE_SYSTEM_CELL||role==IA2_ROLE_SECTION||role==ROLE_SYSTEM_DOCUMENT||role==IA2_ROLE_INTERNAL_FRAME||role==IA2_ROLE_UNKNOWN||role==ROLE_SYSTEM_SEPARATOR) {
		isBlockElement=TRUE;
	} else {
		isBlockElement=FALSE;
	}
	parentNode->setIsBlock(isBlockElement);

	BSTR name=NULL;
	if(pacc->get_accName(varChild,&name)!=S_OK)
		name=NULL;

	BSTR description=NULL;
	if(pacc->get_accDescription(varChild,&description)==S_OK) {
		if(this->hasEncodedAccDescription) {
			if(wcsncmp(description,L"Description: ",13)==0)
				parentNode->addAttribute(L"description",&description[13]);
		} else
			parentNode->addAttribute(L"description",description);
		SysFreeString(description);
	}

	wstring locale;
	IA2Locale ia2Locale={0};
	if(pacc->get_locale(&ia2Locale)==S_OK) {
		if(ia2Locale.language) {
			locale.append(ia2Locale.language);
			SysFreeString(ia2Locale.language);
		}
		if(ia2Locale.country) {
			if(!locale.empty()) {
				locale.append(L"-");
				locale.append(ia2Locale.country);
			}
			SysFreeString(ia2Locale.country);
		}
		if(ia2Locale.variant) {
			SysFreeString(ia2Locale.variant);
		}
	}

	long left=0, top=0, width=0, height=0;
	if(pacc->accLocation(&left,&top,&width,&height,varChild)!=S_OK) {
		LOG_DEBUG(L"Error getting accLocation");
	}

	IAccessibleText* paccText=NULL;
	IAccessibleHypertext* paccHypertext=NULL;
	//get IAccessibleText interface
	pacc->QueryInterface(IID_IAccessibleText,(void**)&paccText);
	//Get IAccessibleHypertext interface
	pacc->QueryInterface(IID_IAccessibleHypertext,(void**)&paccHypertext);
	//Get the text from the IAccessibleText interface
	BSTR IA2Text=NULL;
	int IA2TextLength=0;
	if(paccText&&paccText->get_text(0,-1,&IA2Text)==S_OK&&IA2Text)
		IA2TextLength=SysStringLen(IA2Text);
	// Determine whether the text is extraneous whitespace.
	bool IA2TextIsUnneededSpace=true;
	// Whitespace isn't extraneous in editable controls.
	if(IA2TextLength>0&&(role!=ROLE_SYSTEM_TEXT||(states&STATE_SYSTEM_READONLY))&&!(IA2States&IA2_STATE_EDITABLE)) {
		for(int i=0;i<IA2TextLength;++i) {
			if(IA2Text[i]==L'\n'||IA2Text[i]==L'\xfffc'||!iswspace(IA2Text[i])) {
				IA2TextIsUnneededSpace=false;
				break;
			}
		}
	} else
		IA2TextIsUnneededSpace=false;

	bool isVisible = width > 0 && height > 0;
	bool renderChildren = true;
	long childCount=0;
	if(IA2TextIsUnneededSpace||role==ROLE_SYSTEM_COMBOBOX||(role==ROLE_SYSTEM_LIST&&!(states&STATE_SYSTEM_READONLY))||role==IA2_ROLE_EMBEDDED_OBJECT)
		renderChildren = false;
	else {
		if(pacc->get_accChildCount(&childCount)==S_OK) {
			if (childCount > 0) {
				// If a node has children, it's visible.
				isVisible = true;
			}
		} else
			childCount=0;
	}

	// Handle table cell information.
	IAccessibleTableCell* paccTableCell = NULL;
	// If paccTable is not NULL, it is the table interface for the table above this object.
	if ((paccTable2 || paccTable) && (
		pacc->QueryInterface(IID_IAccessibleTableCell, (void**)&paccTableCell) == S_OK || // IAccessibleTable2
		(IA2AttribsMapIt = IA2AttribsMap.find(L"table-cell-index")) != IA2AttribsMap.end() // IAccessibleTable
	)) {
		// tableID is the IAccessible2::uniqueID for paccTable.
		s << tableID;
		parentNode->addAttribute(L"table-id", s.str());
		s.str(L"");
		if (paccTableCell) {
			// IAccessibleTable2
			this->fillTableCellInfo_IATable2(parentNode, paccTableCell);
			paccTableCell->Release();
			paccTableCell = NULL;
		} else // IAccessibleTable
			fillTableCellInfo_IATable(parentNode, paccTable, IA2AttribsMapIt->second);
		// We're now within a cell, so descendant nodes shouldn't refer to this table anymore.
		paccTable = NULL;
		paccTable2 = NULL;
		tableID = 0;
	}
	// Handle table information.
	// Don't release the table unless it was created in this call.
	bool releaseTable = false;
	// If paccTable is not NULL, we're within a table but not yet within a cell, so don't bother to query for table info.
	if (!paccTable2 && !paccTable) {
		// Try to get table information.
		pacc->QueryInterface(IID_IAccessibleTable2,(void**)&paccTable2);
		if(!paccTable2)
			pacc->QueryInterface(IID_IAccessibleTable,(void**)&paccTable);
		if (paccTable2||paccTable) {
			// We did the QueryInterface for paccTable, so we must release it after all calls that use it are done.
			releaseTable = true;
			// This is a table, so add its information as attributes.
			if((IA2AttribsMapIt = IA2AttribsMap.find(L"layout-guess")) != IA2AttribsMap.end())
				parentNode->addAttribute(L"table-layout",L"1");
			tableID = ID;
			s << ID;
			parentNode->addAttribute(L"table-id", s.str());
			s.str(L"");
			if(paccTable2)
				fillTableCounts<IAccessibleTable2>(parentNode, pacc, paccTable2);
			else
				fillTableCounts<IAccessibleTable>(parentNode, pacc, paccTable);
			// Add the table summary if one is present and the table is visible.
			if (name && isVisible && (tempNode = buffer->addTextFieldNode(parentNode, previousNode, name))) {
				if(!locale.empty()) tempNode->addAttribute(L"language",locale);
				previousNode = tempNode;
			}
		}
	}

	BSTR value=NULL;
	if(pacc->get_accValue(varChild,&value)==S_OK) {
		if(value&&SysStringLen(value)==0) {
			SysFreeString(value);
			value=NULL;
		}
	}

	//If the name isn't being rendered as the content, then add the name as a field attribute.
	if(role!=ROLE_SYSTEM_LINK&&role!=ROLE_SYSTEM_PUSHBUTTON&&role!=ROLE_SYSTEM_GRAPHIC&&name!=NULL) {
		parentNode->addAttribute(L"name",name);
	}

	if (isVisible) {
		if (role==ROLE_SYSTEM_GRAPHIC&&childCount>0&&name) {
			// This is an image map with a name. Render the name first.
			previousNode=buffer->addTextFieldNode(parentNode,previousNode,name);
			if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
		}

		if (renderChildren && IA2TextLength > 0) {
			// Process IAccessibleText.
			int chunkStart=0;
			long attribsStart = 0;
			long attribsEnd = 0;
			map<wstring,wstring> textAttribs;
			for(int i=0;;++i) {
				if(i!=chunkStart&&(i==IA2TextLength||i==attribsEnd||IA2Text[i]==0xfffc)) {
					// We've reached the end of the current chunk of text.
					// (A chunk ends at the end of the text, at the end of an attributes run
					// or at an embedded object char.)
					// Add the chunk to the buffer.
					if((tempNode=buffer->addTextFieldNode(parentNode,previousNode,wstring(IA2Text+chunkStart,i-chunkStart)))!=NULL) {
						previousNode=tempNode;
						// Add text attributes.
						for(map<wstring,wstring>::const_iterator it=textAttribs.begin();it!=textAttribs.end();++it)
							previousNode->addAttribute(it->first,it->second);
						#define copyObjectAttribute(attr) if ((IA2AttribsMapIt = IA2AttribsMap.find(attr)) != IA2AttribsMap.end()) \
							previousNode->addAttribute(attr, IA2AttribsMapIt->second);
						copyObjectAttribute(L"text-align");
						#undef copyObjectAttribute
					}
				}
				if(i==IA2TextLength)
					break;
				if(i==attribsEnd) {
					// We've hit the end of the last attributes run and thus the start of the next.
					textAttribs.clear();
					chunkStart=i;
					BSTR attribsStr;
					if(paccText->get_attributes(attribsEnd,&attribsStart,&attribsEnd,&attribsStr)==S_OK) {
						if(attribsStr) {
							IA2AttribsToMap(attribsStr,textAttribs);
							SysFreeString(attribsStr);
						}
					} else {
						// If attributes fails, assume it'll fail for the entire text.
						attribsEnd=IA2TextLength;
					}
				}
				if(paccHypertext&&IA2Text[i]==0xfffc) {
					// Embedded object char.
					// The next chunk of text shouldn't include this char.
					chunkStart=i+1;
					long hyperlinkIndex;
					if(paccHypertext->get_hyperlinkIndex(i,&hyperlinkIndex)!=S_OK)
						continue;
					IAccessibleHyperlink* paccHyperlink=NULL;
					if(paccHypertext->get_hyperlink(hyperlinkIndex,&paccHyperlink)!=S_OK)
						continue;
					IAccessible2* childPacc=NULL;
					if(paccHyperlink->QueryInterface(IID_IAccessible2,(void**)&childPacc)!=S_OK) {
						paccHyperlink->Release();
						continue;
					}
					paccHyperlink->Release();
					// Skip unneeded graphics in links.
					long childRole;
					BSTR childName=NULL;
					BSTR childDefaction=NULL;
					//role must be link, must have name, 
					//childRole must be graphic, must have no or empty childName, childDefaction can't be click.
					if(role==ROLE_SYSTEM_LINK&&name&&!isWhitespace(name)&&childPacc->role(&childRole)==S_OK&&childRole==ROLE_SYSTEM_GRAPHIC&&(childPacc->get_accName(varChild,&childName)!=S_OK||!childName||SysStringLen(childName)==0)&&(childPacc->get_accDefaultAction(varChild,&childDefaction)==S_OK||wcscmp(childDefaction?childDefaction:L"",L"click")!=0)) {
						if(childName) SysFreeString(childName);
						if(childDefaction) SysFreeString(childDefaction);
						childPacc->Release();
						continue;
					}
					if(childName) SysFreeString(childName);
					if(childDefaction) SysFreeString(childDefaction);
					if((tempNode=this->fillVBuf(childPacc,buffer,parentNode,previousNode,paccTable,paccTable2,tableID))!=NULL) {
						previousNode=tempNode;
					} else {
						LOG_DEBUG(L"Error in fillVBuf");
					}
					childPacc->Release();
				}
			}

		} else if (renderChildren && childCount > 0) {
			// The object has no text, but we do want to render its children.
			VARIANT* varChildren;
			if((varChildren=(VARIANT*)malloc(sizeof(VARIANT)*childCount))==NULL) {
				LOG_DEBUG(L"Error allocating varChildren memory");
				return NULL;
			}
			if(AccessibleChildren(pacc,0,childCount,varChildren,&childCount)!=S_OK) {
				LOG_DEBUG(L"AccessibleChildren failed");
				childCount=0;
			}
			for(long i=0;i<childCount;++i) {
				if(varChildren[i].vt==VT_DISPATCH) {
					IAccessible2* childPacc=NULL;
					varChildren[i].pdispVal->QueryInterface(IID_IAccessible2,(void**)&childPacc);
					if(childPacc) {
						if((tempNode=this->fillVBuf(childPacc,buffer,parentNode,previousNode,paccTable,paccTable2,tableID))!=NULL)
							previousNode=tempNode;
						else
							LOG_DEBUG(L"Error in calling fillVBuf");
						childPacc->Release();
					}
				}
				VariantClear(&(varChildren[i]));
			}
			free(varChildren);

		} else {
			// There were no children to render.
			if(role==ROLE_SYSTEM_LINK||role==ROLE_SYSTEM_PUSHBUTTON||role==IA2_ROLE_TOGGLE_BUTTON||role==ROLE_SYSTEM_MENUITEM||(role==ROLE_SYSTEM_TEXT&&(states&STATE_SYSTEM_READONLY)&&!(states&STATE_SYSTEM_FOCUSABLE))) {
				// Use the name as content.
				if(name) {
					previousNode=buffer->addTextFieldNode(parentNode,previousNode,name);
					if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
				} else if((role==ROLE_SYSTEM_LINK)&&(value!=NULL)) {
					// If a link has no name, derive it from the URL.
					previousNode = buffer->addTextFieldNode(parentNode, previousNode, getNameForURL(value));
				} else if(value) {
					previousNode=buffer->addTextFieldNode(parentNode,previousNode,value);
					if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
				} else {
					// If all else fails, render a space so the user can access the node.
					previousNode=buffer->addTextFieldNode(parentNode,previousNode,L" ");
					if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
				}
			} else if(role==ROLE_SYSTEM_GRAPHIC) {
				int isClickable=(wcscmp(defaction?defaction:L"",L"click")==0);
				int inLink=(states&STATE_SYSTEM_LINKED);
				// Unneeded graphics in links are handled elsewhere, so if we see alt="" here, we should ignore alt and fall back.
				// However, if we see alt="" for a clickable, use the alt and don't fall back.
				if(name&&(SysStringLen(name)>0||isClickable)) {
					previousNode=buffer->addTextFieldNode(parentNode,previousNode,name);
					if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
				} else if (value && !isClickable && inLink) {
					// If the graphic is unlabelled and it's within a link,
					// derive the label from the link URL.
					previousNode = buffer->addTextFieldNode(parentNode, previousNode, getNameForURL(value));
				} else if ((IA2AttribsMap.count(L"src") > 0) && (isClickable || inLink)) {
					// If the graphic is unlabelled and it's clickable,
					// derive the label from the graphic URL.
					previousNode = buffer->addTextFieldNode(parentNode, previousNode, getNameForURL(IA2AttribsMap[L"src"]));
				}
			} else {
				if(value) {
					previousNode=buffer->addTextFieldNode(parentNode,previousNode,value);
					if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
				} else if(role!=ROLE_SYSTEM_CELL&&role!=IA2_ROLE_SECTION) {
					// If all else fails, render a space so the user can access the node.
					// Empty table cells are handled later.
					previousNode=buffer->addTextFieldNode(parentNode,previousNode,L" ");
					if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
				}
			}
		}

		if ((role == ROLE_SYSTEM_CELL || role == ROLE_SYSTEM_ROWHEADER || role == ROLE_SYSTEM_COLUMNHEADER||role==IA2_ROLE_UNKNOWN) && parentNode->getLength() == 0) {
			// Always render a space for empty table cells and unknowns.
			previousNode=buffer->addTextFieldNode(parentNode,previousNode,L" ");
			if(previousNode&&!locale.empty()) previousNode->addAttribute(L"language",locale);
			parentNode->setIsBlock(false);
		}
	}

	// Clean up.
	if(name)
		SysFreeString(name);
	if(value)
		SysFreeString(value);
	if(defaction)
		SysFreeString(defaction);
		if (IA2Text)
		SysFreeString(IA2Text);
	if(paccText)
		paccText->Release();
	if(paccHypertext)
		paccHypertext->Release();
	if (releaseTable) {
		if(paccTable2)
			paccTable2->Release();
		else
			paccTable->Release();
	}

	return parentNode;
}

bool getDocumentFrame(HWND* hwnd, long* childID) {
	IAccessible2* pacc=IAccessible2FromIdentifier((int)*hwnd,*childID);
	if (!pacc)
		return false;

	IAccessible2* parentPacc=NULL;
	VARIANT varChild;
	varChild.vt=VT_I4;
	varChild.lVal=*childID;
	VARIANT varDisp;
	if(pacc->accNavigate(NAVRELATION_NODE_CHILD_OF,varChild,&varDisp)!=S_OK) {
		pacc->Release();
		return false;
	}
	pacc->Release();

	if(varDisp.vt!=VT_DISPATCH) {
		VariantClear(&varDisp);
		return false;
	}

	if(varDisp.pdispVal->QueryInterface(IID_IAccessible2,(void**)&parentPacc)!=S_OK) {
		VariantClear(&varDisp);
		return false;
	}
	VariantClear(&varDisp);

	if(parentPacc==pacc) {
		parentPacc->Release();
		return false;
	}

	long role;
	if(parentPacc->role(&role)!=S_OK||role!=IA2_ROLE_INTERNAL_FRAME) {
		parentPacc->Release();
		return false;
	}

	if(parentPacc->get_uniqueID(childID)!=S_OK||*childID>=0) {
		parentPacc->Release();
		return false;
	}

	if(parentPacc->get_windowHandle(hwnd)!=S_OK) {
		parentPacc->Release();
		return false;
	}

	parentPacc->Release();

	return true;
}

void CALLBACK GeckoVBufBackend_t::renderThread_winEventProcHook(HWINEVENTHOOK hookID, DWORD eventID, HWND hwnd, long objectID, long childID, DWORD threadID, DWORD time) {
	switch(eventID) {
		case EVENT_OBJECT_FOCUS:
		case EVENT_SYSTEM_ALERT:
		case IA2_EVENT_TEXT_UPDATED:
		case IA2_EVENT_TEXT_INSERTED:
		case IA2_EVENT_TEXT_REMOVED:
		case EVENT_OBJECT_REORDER:
		case EVENT_OBJECT_NAMECHANGE:
		case EVENT_OBJECT_VALUECHANGE:
		case EVENT_OBJECT_DESCRIPTIONCHANGE:
		case EVENT_OBJECT_STATECHANGE:
		case IA2_EVENT_OBJECT_ATTRIBUTE_CHANGED:
		break;
		default:
		return;
	}
	if(childID>=0||objectID!=OBJID_CLIENT)
		return;
	LOG_DEBUG(L"winEvent for window "<<hwnd);
	hwnd=findRealMozillaWindow(hwnd);
	if(!hwnd) {
		LOG_DEBUG(L"Invalid window");
		return;
	}
	int docHandle=(int)hwnd;
	int ID=childID;
	VBufBackend_t* backend=NULL;
	for(VBufBackendSet_t::iterator i=runningBackends.begin();i!=runningBackends.end();++i) {
		HWND rootWindow=(HWND)((*i)->rootDocHandle);
		if(rootWindow==hwnd||IsChild(rootWindow,hwnd))
			backend=(*i);
		else
			continue;
		LOG_DEBUG(L"found active backend for this window at "<<backend);

		//For focus and alert events, force any invalid nodes to be updated right now
		if(eventID==EVENT_OBJECT_FOCUS||eventID==EVENT_SYSTEM_ALERT) {
			backend->forceUpdate();
			continue;
		}

		//Ignore state change events on the root node (document) as it can cause rerendering when the document goes busy
		if(eventID==EVENT_OBJECT_STATECHANGE&&hwnd==(HWND)(backend->rootDocHandle)&&childID==backend->rootID)
			return;

		VBufStorage_controlFieldNode_t* node=backend->getControlFieldNodeWithIdentifier(docHandle,ID);
		if(node==NULL&&eventID==EVENT_OBJECT_STATECHANGE) {
			// This event is possibly due to a new document loading in a subframe.
			// Gecko doesn't fire a reorder on the iframe (Mozilla bug 420845), so we need to use NODE_CHILD_OF in this case so that frames will reload.
			LOG_DEBUG(L"State change on an unknown node in a subframe, try NODE_CHILD_OF");
			if (getDocumentFrame(&hwnd, &childID)) {
				#ifdef DEBUG
				Beep(2000,50);
				#endif
				LOG_DEBUG(L"Got NODE_CHILD_OF, recursing");
				renderThread_winEventProcHook(hookID,eventID,hwnd,OBJID_CLIENT,childID,threadID,time);
			} else
				LOG_DEBUG(L"NODE_CHILD_OF failed, returning");
			continue;
		}
		if(!node)
			continue;
		backend->invalidateSubtree(node);
	}
}

void GeckoVBufBackend_t::renderThread_initialize() {
	registerWinEventHook(renderThread_winEventProcHook);
	VBufBackend_t::renderThread_initialize();
}

void GeckoVBufBackend_t::renderThread_terminate() {
	unregisterWinEventHook(renderThread_winEventProcHook);
	VBufBackend_t::renderThread_terminate();
}

void GeckoVBufBackend_t::render(VBufStorage_buffer_t* buffer, int docHandle, int ID, VBufStorage_controlFieldNode_t* oldNode) {
	IAccessible2* pacc=IAccessible2FromIdentifier(docHandle,ID);
	if(!pacc) {
		LOG_DEBUG(L"Could not get IAccessible2, returning");
		return;
	}
	if (!oldNode) {
		// This is the root node.
		this->versionSpecificInit(pacc);
	}
	this->fillVBuf(pacc, buffer, NULL, NULL);
	pacc->Release();
}

GeckoVBufBackend_t::GeckoVBufBackend_t(int docHandle, int ID): VBufBackend_t(docHandle,ID) {
}

GeckoVBufBackend_t::~GeckoVBufBackend_t() {
}

extern "C" __declspec(dllexport) VBufBackend_t* VBufBackend_create(int docHandle, int ID) {
	VBufBackend_t* backend=new GeckoVBufBackend_t(docHandle,ID);
	return backend;
}

BOOL WINAPI DllMain(HINSTANCE hModule,DWORD reason,LPVOID lpReserved) {
	if(reason==DLL_PROCESS_ATTACH) {
		_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL,(_CRT_REPORT_HOOKW)NVDALogCrtReportHook);
	}
	return true;
}
