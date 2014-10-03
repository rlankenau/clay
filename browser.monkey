Strict



Import main



Class BrowserButton Extends ButtonGadget
	Field image:Image
	
	Method New( x:Int, y:Int, image:Image )
		Self.x = x; Self.y = y
		w = 9; h = 9
		Self.image = image
	End
	
	Method OnRender:Void()
		SetColor 255, 255, 255
		DrawImage image, 0, 0
	End
End



Class Browser Extends ContainerGadget
	Field tabs := New List< TabGadget >()
	Field openButton:BrowserButton
	Field newButton:BrowserButton
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Super.New( x, y, w, h )
		openButton = New BrowserButton( 0, 5, imgOpen )
		newButton = New BrowserButton( 0, 5, imgNew )
		AddChild( openButton )
		AddChild( newButton )
	End
	
	Method AddTab:Void( name:String )
		Local tab:TabGadget = New TabGadget( name )
		If Not tabs.IsEmpty() Then tab.x = tabs.First().x + 1
		AddChild( tab )
		tabs.AddFirst( tab )
	End
	
	Method RemoveTab:Void( tab:TabGadget )
		tabs.RemoveEach( tab )
		children.RemoveEach( tab )
		window.children.Remove( tab.project )
		If APP.project = tab.project Then APP.project = Null
	End
	
	Method HandleGadgetEvent:Void( event:GadgetEvent )
		If TabGadget( event.source ) <> Null
			Local top:TabGadget = TabGadget( children.Last() )
			If top = event.source Then Return
			children.RemoveLast()
			
			Local tabPrevious:TabGadget
			
			For Local tab:TabGadget = EachIn tabs
				If tab = top
					Exit
				EndIf
				
				tabPrevious = tab	
			Next
			
			If tabPrevious = Null
				children.AddFirst( top )
			Else
				children.InsertAfter( tabPrevious, top )
			EndIf
			
			children.RemoveEach( event.source )
			children.AddLast( event.source )
		ElseIf event.source = openButton
			Local path:String = OpenFileName()
			If path <> "" Then AddTab( path )
		Else
			'new button
			AddTab( "" )
		EndIf
	End
	
	Method OnRender:Void()
		Local x:Int = 4
		
		If Not tabs.IsEmpty()
			Local tabsNew := New List< TabGadget >()
			
			Repeat
				Local champ:TabGadget, champX:Int = 9999
				
				For Local tab:TabGadget = EachIn tabs
					If tab.x < champX
						champ = tab
						champX = tab.x
					EndIf
				Next
				
				tabs.RemoveEach( champ )
				tabsNew.AddFirst( champ )
			Until tabs.IsEmpty()
			
			tabs = tabsNew
			APP.project = Null
			
			For Local tab:TabGadget = EachIn tabs.Backwards()
				If tab.locked
					tab.x = x
				EndIf
				
				If TabGadget( children.Last() ) = tab
					tab.chosen = True
					APP.project = tab.project
				Else
					tab.chosen = False
				EndIf
				x += tab.w - 7
			Next
		EndIf
		
		openButton.x = x + 11
		newButton.x = x + 11 + 9 + 5
	End
	
	Method OnRenderInterior:Void()
		Super.OnRenderInterior()
		PopMatrix()
		SetColor 255, 255, 255
		DrawLine 0, h - 1, w, h - 1
		
		Local top:TabGadget = TabGadget( children.Last() )
		
		If top <> Null
			SetColor 0, 0, 0
			DrawLine top.x + 1, h - 1, top.x + top.w - 1, h - 1
		EndIf
		
		PushMatrix()
	End
End



Class TabGadget Extends ContainerGadget
	Field name:String
	Field locked:Bool = True
	Field chosen:Bool = False
	Field saveButton:BrowserButton
	Field closeButton:BrowserButton
	
	Field project:Project
	
	Method New( path:String )
		Self.name = StripAll( path )
		w = TextWidth( name ) + 18 + 34
		h = 18
		saveButton = New BrowserButton( 8 + TextWidth( Self.name ) + 11, 5, imgSave )
		closeButton = New BrowserButton( 8 + TextWidth( name ) + 11 + 9 + 5, 5, imgClose )
		AddChild( saveButton )
		AddChild( closeButton )
		project = New Project( path )
	End
	
	Method HandleEvent:Gadget( event:Event )
		Local gadget:Gadget = Super.HandleEvent( event )
		If gadget <> Self Then Return gadget
		
		Select event.id 
			Case EVENT_MOUSE_DOWN_LEFT
				locked = False
				parent.HandleGadgetEvent( New GadgetEvent( Self ) ) 'this is getting dumb, should just call a method
				sx = x
			Case EVENT_MOUSE_DRAG_LEFT
				x = Max( 4, sx + event.dx )
			Case EVENT_MOUSE_UP_LEFT
				locked = True
			Default
		End
		
		Return Self
	End
	
	Method HandleGadgetEvent:Void( event:GadgetEvent )
		If event.source = closeButton 
			Browser( parent ).RemoveTab( Self )
		ElseIf event.source = saveButton
			Local path:String = SaveFileName()
		EndIf
	End
	
	Method OnRender:Void()
		Local name:String = Self.name
		If chosen Then name += "*"
		SetColor 255, 255, 255
		imgTab.Draw( 0, 0, w, h )
		DrawText name, 8, 2
	End
End