//
// Urho3D Engine
// Copyright (c) 2008-2011 Lasse ��rni
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "BorderImage.h"
#include "Context.h"
#include "InputEvents.h"
#include "ListView.h"
#include "Log.h"
#include "StringUtils.h"
#include "UIEvents.h"

#include "DebugNew.h"

static const ShortStringHash indentHash("Indent");

static const std::string highlightModes[] =
{
    "never",
    "focus",
    "always"
};

int GetItemIndent(UIElement* item)
{
    if (!item)
        return 0;
    return item->GetUserData()[indentHash].GetInt();
}

OBJECTTYPESTATIC(ListView);

ListView::ListView(Context* context) :
    ScrollView(context),
    highlightMode_(HM_FOCUS),
    multiselect_(false),
    hierarchyMode_(false),
    clearSelectionOnDefocus_(false),
    doubleClickInterval_(0.5f),
    doubleClickTimer_(0.0f),
    lastClickedItem_(M_MAX_UNSIGNED)
{
    UIElement* container = new UIElement(context_);
    container->SetActive(true);
    container->SetLayout(LM_VERTICAL);
    SetContentElement(container);
    
    SubscribeToEvent(E_UIMOUSECLICK, HANDLER(ListView, HandleUIMouseClick));
}

ListView::~ListView()
{
}

void ListView::RegisterObject(Context* context)
{
    context->RegisterFactory<ListView>();
}

void ListView::SetStyle(const XMLElement& element)
{
    ScrollView::SetStyle(element);
    
    UIElement* root = GetRootElement();
    XMLElement itemElem = element.GetChildElement("listitem");
    if (root)
    {
        while (itemElem)
        {
            if (itemElem.HasAttribute("name"))
            {
                UIElement* item = root->GetChild(itemElem.GetString("name"), true);
                AddItem(item);
                if (itemElem.HasAttribute("indent"))
                    item->GetUserData()[indentHash] = itemElem.GetInt("indent");
                itemElem = itemElem.GetNextElement("listitem");
            }
        }
    }
    
    if (element.HasChildElement("highlight"))
    {
        std::string highlight = element.GetChildElement("highlight").GetStringLower("value");
        SetHighlightMode((HighlightMode)GetStringListIndex(highlight, highlightModes, 3, 1));
    }
    if (element.HasChildElement("multiselect"))
        SetMultiselect(element.GetChildElement("multiselect").GetBool("enable"));
    if (element.HasChildElement("hierarchy"))
        SetHierarchyMode(element.GetChildElement("hierarchy").GetBool("enable"));
    if (element.HasChildElement("clearselection"))
        SetClearSelectionOnDefocus(element.GetChildElement("clearselection").GetBool("enable"));
    if (element.HasChildElement("doubleclickinterval"))
        SetDoubleClickInterval(element.GetChildElement("doubleclickinterval").GetFloat("value"));
    
    XMLElement selectionElem = element.GetChildElement("selection");
    while (selectionElem)
    {
        AddSelection(selectionElem.GetInt("value"));
        selectionElem = selectionElem.GetNextElement("selection");
    }
}

void ListView::Update(float timeStep)
{
    if (doubleClickTimer_ > 0.0f)
        doubleClickTimer_ = Max(doubleClickTimer_ - timeStep, 0.0f);
}

void ListView::OnKey(int key, int buttons, int qualifiers)
{
    // If no selection, can not move with keys
    unsigned numItems = GetNumItems();
    unsigned selection = GetSelection();
    
    if ((selection != M_MAX_UNSIGNED) && (numItems))
    {
        // If either shift or ctrl held down, add to selection if multiselect enabled
        bool additive = (multiselect_) && (qualifiers != 0);
        
        switch (key)
        {
        case KEY_LEFT:
            if (hierarchyMode_)
            {
                SetChildItemsVisible(selection, false);
                return;
            }
            break;
            
        case KEY_RIGHT:
            if (hierarchyMode_)
            {
                SetChildItemsVisible(selection, true);
                return;
            }
            break;
            
        case KEY_RETURN:
            if (hierarchyMode_)
            {
                ToggleChildItemsVisible(selection);
                return;
            }
            break;
            
        case KEY_UP:
            ChangeSelection(-1, additive);
            return;
            
        case KEY_DOWN:
            ChangeSelection(1, additive);
            return;
            
        case KEY_PAGEUP:
            {
                // Convert page step to pixels and see how many items have to be skipped to reach that many pixels
                int stepPixels = ((int)(pageStep_ * scrollPanel_->GetHeight())) - GetSelectedItem()->GetHeight();
                unsigned newSelection = selection;
                unsigned okSelection = selection;
                while (newSelection < numItems)
                {
                    UIElement* item = GetItem(newSelection);
                    int height = 0;
                    if (item->IsVisible())
                    {
                        height = item->GetHeight();
                        okSelection = newSelection;
                    }
                    if (stepPixels < height)
                        break;
                    stepPixels -= height;
                    --newSelection;
                }
                if (!additive)
                    SetSelection(okSelection);
                else
                    AddSelection(okSelection);
            }
            return;
            
        case KEY_PAGEDOWN:
            {
                int stepPixels = ((int)(pageStep_ * scrollPanel_->GetHeight())) - GetSelectedItem()->GetHeight();
                unsigned newSelection = selection;
                unsigned okSelection = selection;
                while (newSelection < numItems)
                {
                    UIElement* item = GetItem(newSelection);
                    int height = 0;
                    if (item->IsVisible())
                    {
                        height = item->GetHeight();
                        okSelection = newSelection;
                    }
                    if (stepPixels < height)
                        break;
                    stepPixels -= height;
                    ++newSelection;
                }
                if (!additive)
                    SetSelection(okSelection);
                else
                    AddSelection(okSelection);
            }
            return;
            
        case KEY_HOME:
            ChangeSelection(-(int)GetNumItems(), additive);
            return;
            
        case KEY_END:
            ChangeSelection(GetNumItems(), additive);
            return;
        }
    }
    
    using namespace UnhandledKey;
    
    VariantMap eventData;
    eventData[P_ELEMENT] = (void*)this;
    eventData[P_KEY] = key;
    eventData[P_BUTTONS] = buttons;
    eventData[P_QUALIFIERS] = qualifiers;
    SendEvent(E_UNHANDLEDKEY, eventData);
}

void ListView::OnResize()
{
    ScrollView::OnResize();
    
    // Set the content element width to match the scrollpanel
    const IntRect& clipBorder = scrollPanel_->GetClipBorder();
    contentElement_->SetWidth(scrollPanel_->GetWidth() - clipBorder.left_ - clipBorder.right_);
}

void ListView::OnFocus()
{
    UpdateSelectionEffect();
}

void ListView::OnDefocus()
{
    if (clearSelectionOnDefocus_)
        ClearSelection();
    
    UpdateSelectionEffect();
}

void ListView::AddItem(UIElement* item)
{
    InsertItem(contentElement_->GetNumChildren(), item);
}

void ListView::InsertItem(unsigned index, UIElement* item)
{
    if ((!item) || (item->GetParent() == contentElement_))
        return;
    
    // Enable input so that clicking the item can be detected
    item->SetActive(true);
    item->SetSelected(false);
    contentElement_->InsertChild(index, item);
    
    // If necessary, shift the following selections
    std::set<unsigned> prevSelections;
    selections_.clear();
    for (std::set<unsigned>::iterator j = prevSelections.begin(); j != prevSelections.end(); ++j)
    {
        if (*j >= index)
            selections_.insert(*j + 1);
        else
            selections_.insert(*j);
    }
    UpdateSelectionEffect();
}

void ListView::RemoveItem(UIElement* item)
{
    unsigned numItems = GetNumItems();
    
    for (unsigned i = 0; i < numItems; ++i)
    {
        if (GetItem(i) == item)
        {
            item->SetSelected(false);
            selections_.erase(i);
            
            // Remove any child items in hierarchy mode
            unsigned removed = 1;
            if (hierarchyMode_)
            {
                int baseIndent = GetItemIndent(item);
                int j = i + 1;
                for (;;)
                {
                    UIElement* childItem = GetItem(i + 1);
                    if (!childItem)
                        break;
                    if (GetItemIndent(childItem) > baseIndent)
                    {
                        childItem->SetSelected(false);
                        selections_.erase(j);
                        contentElement_->RemoveChild(childItem);
                        ++removed;
                    }
                    else
                        break;
                    ++j;
                }
            }
            
            // If necessary, shift the following selections
            std::set<unsigned> prevSelections;
            selections_.clear();
            for (std::set<unsigned>::iterator j = prevSelections.begin(); j != prevSelections.end(); ++j)
            {
                if (*j > i)
                    selections_.insert(*j - removed);
                else
                    selections_.insert(*j);
            }
            UpdateSelectionEffect();
            break;
        }
    }
    contentElement_->RemoveChild(item);
}

void ListView::RemoveItem(unsigned index)
{
    RemoveItem(GetItem(index));
}

void ListView::RemoveAllItems()
{
    unsigned numItems = GetNumItems();
    for (unsigned i = 0; i < numItems; ++i)
        contentElement_->GetChild(i)->SetSelected(false);
    contentElement_->RemoveAllChildren();
    ClearSelection();
}

void ListView::SetSelection(unsigned index)
{
    std::set<unsigned> indices;
    indices.insert(index);
    SetSelections(indices);
    EnsureItemVisibility(index);
}

void ListView::SetSelections(const std::set<unsigned>& indices)
{
    unsigned numItems = GetNumItems();
    
    // Remove first items that should no longer be selected
    for (std::set<unsigned>::iterator i = selections_.begin(); i != selections_.end();)
    {
        unsigned index = *i;
        if (indices.find(index) == indices.end())
        {
            std::set<unsigned>::iterator current = i++;
            selections_.erase(current);
            
            using namespace Iteselected_;
            
            VariantMap eventData;
            eventData[P_ELEMENT] = (void*)this;
            eventData[P_SELECTION] = index;
            SendEvent(E_ITEMDESELECTED, eventData);
        }
        else
            ++i;
    }
    
    // Then add missing items
    for (std::set<unsigned>::const_iterator i = indices.begin(); i != indices.end(); ++i)
    {
        unsigned index = *i;
        if (index < numItems)
        {
            // In singleselect mode, resend the event even for the same selection
            if ((selections_.find(index) == selections_.end()) || (!multiselect_))
            {
                selections_.insert(*i);
                
                using namespace Iteselected_;
                
                VariantMap eventData;
                eventData[P_ELEMENT] = (void*)this;
                eventData[P_SELECTION] = *i;
                SendEvent(E_ITEMSELECTED, eventData);
            }
        }
        // If no multiselect enabled, allow setting only one item
        if (!multiselect_)
            break;
    }
    
    UpdateSelectionEffect();
}

void ListView::AddSelection(unsigned index)
{
    if (!multiselect_)
        SetSelection(index);
    else
    {
        if (index >= GetNumItems())
            return;
        
        std::set<unsigned> newSelections = selections_;
        newSelections.insert(index);
        SetSelections(newSelections);
        EnsureItemVisibility(index);
    }
}

void ListView::RemoveSelection(unsigned index)
{
    if (index >= GetNumItems())
        return;
    
    std::set<unsigned> newSelections = selections_;
    newSelections.erase(index);
    SetSelections(newSelections);
    EnsureItemVisibility(index);
}

void ListView::ToggleSelection(unsigned index)
{
    unsigned numItems = GetNumItems();
    if (index >= numItems)
        return;
    
    if (selections_.find(index) != selections_.end())
        RemoveSelection(index);
    else
        AddSelection(index);
}

void ListView::ChangeSelection(int delta, bool additive)
{
    if (selections_.empty())
        return;
    if (!multiselect_)
        additive = false;
    
    // If going downwards, use the last selection as a base. Otherwise use first
    unsigned selection = delta > 0 ? *selections_.rbegin() : *selections_.begin();
    unsigned numItems = GetNumItems();
    unsigned newSelection = selection;
    unsigned okSelection = selection;
    while (delta != 0)
    {
        if (delta > 0)
        {
            ++newSelection;
            if (newSelection >= numItems)
                break;
        }
        if (delta < 0)
        {
            --newSelection;
            if (newSelection >= numItems)
                break;
        }
        UIElement* item = GetItem(newSelection);
        if (item->IsVisible())
        {
            okSelection = newSelection;
            if (delta > 0)
                --delta;
            if (delta < 0)
                ++delta;
        }
    }
    
    if (!additive)
        SetSelection(okSelection);
    else
        AddSelection(okSelection);
}

void ListView::ClearSelection()
{
    SetSelections(std::set<unsigned>());
    UpdateSelectionEffect();
}

void ListView::SetHighlightMode(HighlightMode mode)
{
    highlightMode_ = mode;
    UpdateSelectionEffect();
}

void ListView::SetMultiselect(bool enable)
{
    multiselect_ = enable;
}

void ListView::SetHierarchyMode(bool enable)
{
    hierarchyMode_ = enable;
}

void ListView::SetClearSelectionOnDefocus(bool enable)
{
    clearSelectionOnDefocus_ = enable;
}

void ListView::SetDoubleClickInterval(float interval)
{
    doubleClickInterval_ = interval;
}

void ListView::SetChildItemsVisible(unsigned index, bool enable)
{
    unsigned numItems = GetNumItems();
    
    if ((!hierarchyMode_) || (index >= numItems))
        return;
    
    int baseIndent = GetItemIndent(GetItem(index));
    
    for (unsigned i = index + 1; i < numItems; ++i)
    {
        UIElement* item = GetItem(i);
        if (GetItemIndent(item) > baseIndent)
            item->SetVisible(enable);
        else
            break;
    }
}

void ListView::SetChildItemsVisible(bool enable)
{
    unsigned numItems = GetNumItems();
    
    for (unsigned i = 0; i < numItems; ++i)
    {
        if (!GetItemIndent(GetItem(i)))
            SetChildItemsVisible(i, enable);
    }
    
    if (GetSelections().size() == 1)
        EnsureItemVisibility(GetSelection());
}

void ListView::ToggleChildItemsVisible(unsigned index)
{
    unsigned numItems = GetNumItems();
    
    if ((!hierarchyMode_) || (index >= numItems))
        return;
    
    int baseIndent = GetItemIndent(GetItem(index));
    bool firstChild = true;
    UIElement* prevItem = 0;
    for (unsigned i = index + 1; i < numItems; ++i)
    {
        UIElement* item = GetItem(i);
        if (GetItemIndent(item) > baseIndent)
        {
            if (firstChild)
            {
                item->SetVisible(!item->IsVisible());
                firstChild = false;
            }
            else
                item->SetVisible(prevItem->IsVisible());
        }
        else
            break;
        
        prevItem = item;
    }
}

unsigned ListView::GetNumItems() const
{
    return contentElement_->GetNumChildren();
}

UIElement* ListView::GetItem(unsigned index) const
{
    return contentElement_->GetChild(index);
}

std::vector<UIElement*> ListView::GetItems() const
{
    return contentElement_->GetChildren();
}

unsigned ListView::GetSelection() const
{
    if (selections_.empty())
        return M_MAX_UNSIGNED;
    else
        return *selections_.begin();
}

UIElement* ListView::GetSelectedItem() const
{
    return contentElement_->GetChild(GetSelection());
}

std::vector<UIElement*> ListView::GetSelectedItems() const
{
    std::vector<UIElement*> ret;
    for (std::set<unsigned>::const_iterator i = selections_.begin(); i != selections_.end(); ++i)
    {
        UIElement* item = GetItem(*i);
        if (item)
            ret.push_back(item);
    }
    return ret;
}

void ListView::UpdateSelectionEffect()
{
    unsigned numItems = GetNumItems();
    
    for (unsigned i = 0; i < numItems; ++i)
    {
        UIElement* item = GetItem(i);
        if ((highlightMode_ != HM_NEVER) && (selections_.find(i) != selections_.end()))
            item->SetSelected((focus_) || (highlightMode_ == HM_ALWAYS));
        else
            item->SetSelected(false);
    }
}

void ListView::EnsureItemVisibility(unsigned index)
{
    UIElement* item = GetItem(index);
    if ((!item) || (!item->IsVisible()))
        return;
    
    IntVector2 currentOffset = item->GetScreenPosition() - scrollPanel_->GetScreenPosition() - contentElement_->GetPosition();
    IntVector2 newView = GetViewPosition();
    const IntRect& clipBorder = scrollPanel_->GetClipBorder();
    IntVector2 windowSize(scrollPanel_->GetWidth() - clipBorder.left_ - clipBorder.right_, scrollPanel_->GetHeight() -
        clipBorder.top_ - clipBorder.bottom_);
    
    if (currentOffset.y_ < 0)
        newView.y_ += currentOffset.y_;
    if (currentOffset.y_ + item->GetHeight() > windowSize.y_)
        newView.y_ += currentOffset.y_ + item->GetHeight() - windowSize.y_;
    
    SetViewPosition(newView);
}

void ListView::HandleUIMouseClick(StringHash eventType, VariantMap& eventData)
{
    if (eventData[UIMouseClick::P_BUTTON].GetInt() != MOUSEB_LEFT)
        return;
    int qualifiers = eventData[UIMouseClick::P_QUALIFIERS].GetInt();
    
    UIElement* element = static_cast<UIElement*>(eventData[UIMouseClick::P_ELEMENT].GetPtr());
    
    unsigned numItems = GetNumItems();
    for (unsigned i = 0; i < numItems; ++i)
    {
        if (element == GetItem(i))
        {
            // Check doubleclick
            bool isDoubleClick = false;
            if ((!multiselect_) || (!qualifiers))
            {
                if ((doubleClickTimer_ > 0.0f) && (lastClickedItem_ == i))
                {
                    isDoubleClick = true;
                    doubleClickTimer_ = 0.0f;
                }
                else
                {
                    doubleClickTimer_ = doubleClickInterval_;
                    lastClickedItem_ = i;
                }
                SetSelection(i);
            }
            
            // Check multiselect with shift & ctrl
            if (multiselect_)
            {
                if (qualifiers & QUAL_SHIFT)
                {
                    if (selections_.empty())
                        SetSelection(i);
                    else
                    {
                        unsigned first = *selections_.begin();
                        unsigned last = *selections_.rbegin();
                        std::set<unsigned> newSelections = selections_;
                        if ((i == first) || (i == last))
                        {
                            for (unsigned j = first; j <= last; ++j)
                                newSelections.insert(j);
                        }
                        else if (i < first)
                        {
                            for (unsigned j = i; j <= first; ++j)
                                newSelections.insert(j);
                        }
                        else if (i < last)
                        {
                            if ((abs((int)i - (int)first)) <= (abs((int)i - (int)last)))
                            {
                                for (unsigned j = first; j <= i; ++j)
                                    newSelections.insert(j);
                            }
                            else
                            {
                                for (unsigned j = i; j <= last; ++j)
                                    newSelections.insert(j);
                            }
                        }
                        else if (i > last)
                        {
                            for (unsigned j = last; j <= i; ++j)
                                newSelections.insert(j);
                        }
                        SetSelections(newSelections);
                    }
                }
                else if (qualifiers & QUAL_CTRL)
                    ToggleSelection(i);
            }
            
            if (isDoubleClick)
            {
                if (hierarchyMode_)
                    ToggleChildItemsVisible(i);
                
                VariantMap eventData;
                eventData[ItemDoubleClicked::P_ELEMENT] = (void*)this;
                eventData[ItemDoubleClicked::P_SELECTION] = i;
                SendEvent(E_ITEMDOUBLECLICKED, eventData);
            }
            
            return;
        }
    }
}
