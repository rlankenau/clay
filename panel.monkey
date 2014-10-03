Strict



Import main



Class Panel Extends ContainerGadget
	Field yMax:Int = 50
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Self.x = x; Self.y = y; Self.w = w; Self.h = h
	End
	
	Method AddChild:Void( child:Gadget )
		Super.AddChild( child )
	End
	
	Method HandleEvent:Gadget( event:Event )
		Local gadget:Gadget = Super.HandleEvent( event )
		If gadget <> Self Then Return gadget
		
		Select event.id
			Case EVENT_MOUSE_DRAG_RIGHT
				oy = sy + event.dy
				oy = -Max( 0, Min( -oy, yMax ) )
			Case EVENT_MOUSE_DOWN_RIGHT
				sy = oy
			Default
		End
		
		Return Self
	End
	
	Method HandleGadgetEvent:Void( event:GadgetEvent )
		Select True
			Case Slider( event.source ) <> Null
				Local s:Slider = Slider( event.source )
				APP.project.boxSelected.settings.Get( s.name ).value = s.index
				_ExecuteBoxSelectedIfSatisfied()
			Case NumberBox( event.source ) <> Null
				Local n:NumberBox = NumberBox( event.source )
				APP.project.boxSelected.settings.Get( n.name ).value = n.value 
				_ExecuteBoxSelectedIfSatisfied()
			Case DropList( event.source ) <> Null
				Local d:DropList = DropList( event.source )
				APP.project.boxSelected.settings.Get( d.name ).value = d.index
				_ExecuteBoxSelectedIfSatisfied()
			Case CheckBox( event.source ) <> Null
				Local c:CheckBox = CheckBox( event.source )
				APP.project.boxSelected.settings.Get( c.name ).value = c.on
				_ExecuteBoxSelectedIfSatisfied()
			Case RuleTable( event.source ) <> Null
				_ExecuteBoxSelectedIfSatisfied()
			Default
		End
	End
	
	Method _ExecuteBoxSelectedIfSatisfied:Void()
		Local satisfied:Bool[ APP.project.boxSelected.ins ]
		
		For Local wire:Wire = EachIn APP.patch.wires
			If wire.b = APP.project.boxSelected And wire.a.done
				satisfied[ wire.bId ] = True
			EndIf
		Next
		
		For Local n:Int = 0 Until satisfied.Length
			If satisfied[n] = False Then Return
		Next
		
		APP.project.boxSelected.Execute()
	End
	
	Method OnRender:Void()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	End
	
	Method OnRenderInterior:Void()
		Local y:Int = 4
		
		For Local child:Gadget = EachIn children
			child.y = y
			y += child.h + 4	
		Next
		
		Super.OnRenderInterior()
	End
End